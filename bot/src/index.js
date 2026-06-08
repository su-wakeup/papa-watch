// papa-watch-bot — Cloudflare Worker that bridges Telegram → MQTT → Stanley's watch.
//
//   Telegram                     this Worker                       broker.emqx.io                  watch
//   ────────                     ───────────                       ──────────────                  ─────
//   /heart           ── webhook ────► verify secret
//                                     parse command                ── PUBLISH ────► stopwatch/<pair>/from-dad
//                                                                                   payload: {t, from, emoji}
//                                                                                                  │
//                                                                                                  ▼
//                                                                                              lub-dub buzz
//
// MQTT 3.1.1 over WebSocket (subprotocol "mqtt"), no auth, QoS 0.
// We CONNECT → PUBLISH → DISCONNECT in a single request lifetime.

import { decodeVoiceToPcm16, pcm16ToWav, tgGetFileBytes, tgSendAudioWav } from "./voice.js";

const PROTOCOL_NAME = new Uint8Array([0x00, 0x04, 0x4D, 0x51, 0x54, 0x54]); // "MQTT"
const PROTOCOL_LEVEL = 0x04;                                                // 3.1.1
const FLAGS_CLEAN_SESSION = 0x02;
const KEEPALIVE = 60;

function encodeUtf8Prefixed(str) {
    const bytes = new TextEncoder().encode(str);
    const out = new Uint8Array(2 + bytes.length);
    out[0] = (bytes.length >> 8) & 0xff;
    out[1] = bytes.length & 0xff;
    out.set(bytes, 2);
    return out;
}

function encodeRemainingLength(n) {
    const out = [];
    do {
        let byte = n & 0x7f;
        n >>= 7;
        if (n > 0) byte |= 0x80;
        out.push(byte);
    } while (n > 0);
    return new Uint8Array(out);
}

function concatBytes(...arrays) {
    const total = arrays.reduce((acc, a) => acc + a.length, 0);
    const out = new Uint8Array(total);
    let off = 0;
    for (const a of arrays) { out.set(a, off); off += a.length; }
    return out;
}

function makeConnect(clientId) {
    const cidEnc = encodeUtf8Prefixed(clientId);
    const variable = concatBytes(
        PROTOCOL_NAME,
        new Uint8Array([PROTOCOL_LEVEL, FLAGS_CLEAN_SESSION, (KEEPALIVE >> 8) & 0xff, KEEPALIVE & 0xff]),
    );
    const body = concatBytes(variable, cidEnc);
    return concatBytes(new Uint8Array([0x10]), encodeRemainingLength(body.length), body);
}

function makePublish(topic, payload, retain = false) {
    const topicEnc = encodeUtf8Prefixed(topic);
    const payloadBytes = typeof payload === "string" ? new TextEncoder().encode(payload) : payload;
    const body = concatBytes(topicEnc, payloadBytes);
    // 0x30 = PUBLISH (QoS 0, no retain); 0x31 sets the RETAIN bit so the
    // broker remembers the last payload per topic and replays it to new
    // subscribers — critical for the watch which may be offline at publish.
    const firstByte = retain ? 0x31 : 0x30;
    return concatBytes(new Uint8Array([firstByte]),
                       encodeRemainingLength(body.length), body);
}

function makeSubscribe(packetId, topic) {
    const topicEnc = encodeUtf8Prefixed(topic);
    const body = concatBytes(
        new Uint8Array([(packetId >> 8) & 0xff, packetId & 0xff]),
        topicEnc,
        new Uint8Array([0x01]),                  // requested QoS 1
    );
    return concatBytes(new Uint8Array([0x82]), encodeRemainingLength(body.length), body);
}

function parsePublishPacket(buf) {
    if ((buf[0] & 0xf0) !== 0x30) return null;
    const qos = (buf[0] >> 1) & 0x03;
    let i = 1;
    let remLen = 0, mult = 1;
    while (i < buf.length) {
        const b = buf[i++];
        remLen += (b & 0x7f) * mult;
        if ((b & 0x80) === 0) break;
        mult *= 128;
    }
    const fixedEnd = i;
    if (i + 2 > buf.length) return null;
    const topicLen = (buf[i] << 8) | buf[i + 1];
    i += 2;
    const topic = new TextDecoder().decode(buf.slice(i, i + topicLen));
    i += topicLen;
    if (qos > 0) i += 2;                          // skip packet id
    const payload = new TextDecoder().decode(buf.slice(i, fixedEnd + remLen));
    return { topic, payload, qos };
}

const DISCONNECT_PACKET = new Uint8Array([0xe0, 0x00]);
const PINGREQ_PACKET    = new Uint8Array([0xc0, 0x00]);

async function publishMqtt(brokerWss, topic, payload, options = {}) {
    // Cloudflare-style outbound WebSocket: fetch with Upgrade headers.
    const response = await fetch(brokerWss.replace(/^wss:/, "https:"), {
        headers: {
            Upgrade: "websocket",
            "Sec-WebSocket-Protocol": "mqtt",
        },
    });
    const ws = response.webSocket;
    if (!ws) throw new Error(`broker did not upgrade: ${response.status}`);
    ws.accept();

    const clientId = `papa-bot-${Date.now().toString(36)}`;
    ws.send(makeConnect(clientId));

    await new Promise((resolve, reject) => {
        const timeout = setTimeout(() => reject(new Error("CONNACK timeout 5s")), 5000);

        const onPacket = (data) => {
            if (data[0] === 0x20 /* CONNACK */) {
                clearTimeout(timeout);
                ws.send(makePublish(topic, payload, options.retain === true));
                setTimeout(() => {
                    try { ws.send(DISCONNECT_PACKET); ws.close(); } catch (_) {}
                    resolve();
                }, 80);
            }
        };

        ws.addEventListener("message", async (ev) => {
            try {
                if (ev.data instanceof ArrayBuffer) {
                    onPacket(new Uint8Array(ev.data));
                } else if (ev.data && typeof ev.data.arrayBuffer === "function") {
                    // Cloudflare delivers binary WS frames as Blob
                    const buf = await ev.data.arrayBuffer();
                    onPacket(new Uint8Array(buf));
                }
                // ignore string frames (broker shouldn't send those for MQTT)
            } catch (e) {
                clearTimeout(timeout);
                reject(e);
            }
        });
        ws.addEventListener("error", (e) => {
            clearTimeout(timeout);
            reject(new Error("ws error: " + (e?.message || "")));
        });
        ws.addEventListener("close", () => { clearTimeout(timeout); resolve(); });
    });
}

async function telegramSend(token, chatId, text) {
    return fetch(`https://api.telegram.org/bot${token}/sendMessage`, {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({ chat_id: chatId, text }),
    });
}

// Where PAPA is — name (+ aliases) → lat, lon, POSIX TZ (DST baked in, same
// style as the watch's tz_helper). Lights PAPA's gold dot on the globe.
const WHERE_CITIES = {
    shenzhen:   { label: "Shenzhen",   lat: 22.54, lon: 114.06, tz: "CST-8" },
    beijing:    { label: "Beijing",    lat: 39.90, lon: 116.41, tz: "CST-8" },
    shanghai:   { label: "Shanghai",   lat: 31.23, lon: 121.47, tz: "CST-8" },
    guangzhou:  { label: "Guangzhou",  lat: 23.13, lon: 113.26, tz: "CST-8" },
    hongkong:   { label: "Hong Kong",  lat: 22.32, lon: 114.17, tz: "HKT-8" },
    hk:         { label: "Hong Kong",  lat: 22.32, lon: 114.17, tz: "HKT-8" },
    "los gatos":{ label: "Los Gatos",  lat: 37.23, lon: -121.97,tz: "PST8PDT,M3.2.0,M11.1.0" },
    home:       { label: "Los Gatos",  lat: 37.23, lon: -121.97,tz: "PST8PDT,M3.2.0,M11.1.0" },
    "san jose": { label: "San Jose",   lat: 37.34, lon: -121.89,tz: "PST8PDT,M3.2.0,M11.1.0" },
    sf:         { label: "SF",         lat: 37.77, lon: -122.42,tz: "PST8PDT,M3.2.0,M11.1.0" },
    "san francisco": { label: "SF",    lat: 37.77, lon: -122.42,tz: "PST8PDT,M3.2.0,M11.1.0" },
    la:         { label: "LA",         lat: 34.05, lon: -118.24,tz: "PST8PDT,M3.2.0,M11.1.0" },
    "los angeles": { label: "LA",      lat: 34.05, lon: -118.24,tz: "PST8PDT,M3.2.0,M11.1.0" },
    nyc:        { label: "New York",   lat: 40.71, lon: -74.01, tz: "EST5EDT,M3.2.0,M11.1.0" },
    "new york": { label: "New York",   lat: 40.71, lon: -74.01, tz: "EST5EDT,M3.2.0,M11.1.0" },
    seattle:    { label: "Seattle",    lat: 47.61, lon: -122.33,tz: "PST8PDT,M3.2.0,M11.1.0" },
    tokyo:      { label: "Tokyo",      lat: 35.69, lon: 139.69, tz: "JST-9" },
    seoul:      { label: "Seoul",      lat: 37.57, lon: 127.00, tz: "KST-9" },
    singapore:  { label: "Singapore",  lat: 1.35,  lon: 103.82, tz: "SGT-8" },
    dubai:      { label: "Dubai",      lat: 25.20, lon: 55.27,  tz: "GST-4" },
    london:     { label: "London",     lat: 51.51, lon: -0.13,  tz: "GMT0BST,M3.5.0/1,M10.5.0" },
    paris:      { label: "Paris",      lat: 48.86, lon: 2.35,   tz: "CET-1CEST,M3.5.0,M10.5.0/3" },
    sydney:     { label: "Sydney",     lat: -33.87,lon: 151.21, tz: "AEST-10AEDT,M10.1.0,M4.1.0/3" },
};

const HELP = [
    "Stanley's watch — control panel",
    "",
    "/heart           send a heart (default ❤)",
    "/heart 💕         send with custom emoji",
    "/heart 想你了    send with text (will display on watch as alert)",
    "/status free     set dad state on Stanley's face (6h)",
    "/status busy",
    "/status away",
    "/status thinking",
    "/status auto     clear override, back to time-based auto",
    "/where shenzhen  light PAPA's dot on Stanley's globe",
    "/where los gatos / tokyo / london / ...",
    "/ota             trigger firmware update check",
    "/help            this message",
].join("\n");

async function handleTelegram(update, env, baseUrl) {
    const msg = update.message;
    if (!msg) return;

    const from = msg.from?.username || msg.from?.first_name || "unknown";
    const chatId = msg.chat.id;

    // optional allowlist
    if (env.ALLOWED_USERS) {
        const allowed = env.ALLOWED_USERS.split(",").map((s) => s.trim()).filter(Boolean);
        if (allowed.length && !allowed.includes(from)) {
            await telegramSend(env.TELEGRAM_BOT_TOKEN, chatId, `not authorized: @${from}`);
            return;
        }
    }

    const topic = `stopwatch/${env.PAIR_ID}/from-dad`;

    // PAPA→Stanley voice (roadmap #6): a Telegram voice note (OGG/Opus) →
    // decode to 16 kHz PCM, park it in R2 (a rolling 7-day mailbox), and ping
    // the watch. The watch does NOT auto-play — it buzzes and lists the clip so
    // Stanley plays it when he wants (no blurting out in class).
    if (msg.voice) {
        try {
            const ogg = await tgGetFileBytes(env.TELEGRAM_BOT_TOKEN, msg.voice.file_id);
            const { bytes, samples, truncated } = await decodeVoiceToPcm16(ogg);
            const id = crypto.randomUUID();
            const secs = +(samples / 16000).toFixed(1);
            const ts = Math.floor(Date.now() / 1000);            // epoch seconds (fits uint32)
            await env.EMAILS_R2.put(`voice/${id}.pcm`, bytes, {
                httpMetadata: { contentType: "application/octet-stream" },
                customMetadata: { secs: String(secs), ts: String(ts) },
            });
            // NOT retained: from-dad already holds the retained daily quote and
            // MQTT keeps only one retained payload per topic. This is a best-effort
            // real-time buzz; the server-side inbox (R2 + /inbox) is what makes the
            // clip durable, so a watch that's asleep now still finds it on next open.
            await publishMqtt(env.MQTT_BROKER_WSS, topic,
                JSON.stringify({ cmd: "voice_new", ts, secs }));
            await telegramSend(env.TELEGRAM_BOT_TOKEN, chatId,
                `🎙️ 已存入 Stanley 的语音信箱（${secs}s${truncated ? "，截断至30s" : ""}），保留一周。手表会轻震提醒，他在 PAPA app 里随时可听。`);
        } catch (e) {
            console.error("[voice→watch] failed:", e.stack || e.message);
            await telegramSend(env.TELEGRAM_BOT_TOKEN, chatId, `语音处理失败: ${e.message}`);
        }
        return;
    }

    if (!msg.text) return;
    const text = msg.text.trim();

    if (text === "/start" || text === "/help") {
        await telegramSend(env.TELEGRAM_BOT_TOKEN, chatId, HELP);
        return;
    }

    if (text === "/heart" || text.startsWith("/heart ")) {
        const tail = text.slice(6).trim();
        const payload = JSON.stringify({
            t: Math.floor(Date.now() / 1000),
            from,
            note: tail || "❤",
        });
        try {
            await publishMqtt(env.MQTT_BROKER_WSS, topic, payload);
            await telegramSend(env.TELEGRAM_BOT_TOKEN, chatId, `💕 sent: ${tail || "❤"}`);
        } catch (e) {
            console.error("/heart failed:", e.message);
            await telegramSend(env.TELEGRAM_BOT_TOKEN, chatId, `failed: ${e.message}`);
        }
        return;
    }

    if (text === "/ota") {
        try {
            await publishMqtt(env.MQTT_BROKER_WSS, topic, JSON.stringify({ cmd: "ota" }));
            await telegramSend(env.TELEGRAM_BOT_TOKEN, chatId, "⚙️ OTA check triggered");
        } catch (e) {
            await telegramSend(env.TELEGRAM_BOT_TOKEN, chatId, `failed: ${e.message}`);
        }
        return;
    }

    if (text === "/status" || text.startsWith("/status ")) {
        const arg = text.slice(7).trim().toLowerCase();
        const valid = ["free", "busy", "away", "thinking", "auto"];
        if (!arg || !valid.includes(arg)) {
            await telegramSend(env.TELEGRAM_BOT_TOKEN, chatId,
                `usage: /status [${valid.join("|")}]`);
            return;
        }
        try {
            await publishMqtt(env.MQTT_BROKER_WSS, topic,
                JSON.stringify({ cmd: "status", value: arg }));
            await telegramSend(env.TELEGRAM_BOT_TOKEN, chatId,
                arg === "auto"
                    ? "🕒 status → auto (clears override)"
                    : `✨ status → ${arg.toUpperCase()} (6h)`);
        } catch (e) {
            await telegramSend(env.TELEGRAM_BOT_TOKEN, chatId, `failed: ${e.message}`);
        }
        return;
    }

    if (text === "/where" || text.startsWith("/where ")) {
        const arg = text.slice(6).trim().toLowerCase().replace(/\s+/g, " ");
        const city = WHERE_CITIES[arg];
        if (!arg || !city) {
            const known = [...new Set(Object.values(WHERE_CITIES).map((c) => c.label))].join(", ");
            await telegramSend(env.TELEGRAM_BOT_TOKEN, chatId,
                `usage: /where <city>\nknown: ${known}`);
            return;
        }
        try {
            // Retained: the watch picks it up the moment it next connects, even
            // if it was offline when you sent this.
            await publishMqtt(env.MQTT_BROKER_WSS, topic,
                JSON.stringify({ cmd: "loc", city: city.label,
                                 lat: city.lat, lon: city.lon, tz: city.tz }),
                { retain: true });
            await telegramSend(env.TELEGRAM_BOT_TOKEN, chatId,
                `📍 PAPA → ${city.label} (Stanley's globe lights up there)`);
        } catch (e) {
            await telegramSend(env.TELEGRAM_BOT_TOKEN, chatId, `failed: ${e.message}`);
        }
        return;
    }

    await telegramSend(env.TELEGRAM_BOT_TOKEN, chatId, `unknown: ${text}\n${HELP}`);
}

// ─── Family-events ingest (Phase 1) ────────────────────────────────────────
// Accepts an email-shaped JSON payload, stores the body in R2 and a metadata
// row in D1. Phase 2 (LLM extractor) reads `raw_emails WHERE processed = 0`
// and populates `events`. See bot/schema.sql for the table definitions.
async function handleIngestEmail(request, env, ctx) {
    if (!env.EVENTS_DB) return new Response("D1 not bound (EVENTS_DB)", { status: 500 });
    if (!env.EMAILS_R2) return new Response("R2 not bound (EMAILS_R2)", { status: 500 });

    const payload = await request.json();
    const { from, to = null, subject = null, body } = payload;
    const received_at = payload.received_at || Math.floor(Date.now() / 1000);

    if (!from || !body) {
        return new Response("required: from, body", { status: 400 });
    }

    // Resolve source by email_match substring. Contains-match (not suffix) so
    // it works on Gmail's full From header — `Display Name <addr@host>` — whose
    // trailing '>' would defeat a pure suffix match. NULL = unknown sender; row
    // still gets stored so we can backfill source_id later once we add the match.
    const source = await env.EVENTS_DB.prepare(
        `SELECT id FROM sources
            WHERE email_match IS NOT NULL
              AND ? LIKE '%' || email_match || '%'
            LIMIT 1`
    ).bind(from).first();
    // Anything the Gmail filter forwarded is Lakeside-relevant by construction,
    // but teacher/personal senders won't carry "Lakeside" in their From. Fall
    // back to the Lakeside source (id 1) so their events still fan out instead
    // of stranding at source_id=NULL.
    const source_id = source?.id ?? 1;

    // Insert the row first so we have an id for the R2 key.
    const ins = await env.EVENTS_DB.prepare(
        `INSERT INTO raw_emails
            (source_id, from_address, to_address, subject, received_at, r2_key, bytes)
         VALUES (?, ?, ?, ?, ?, '', ?)`
    ).bind(source_id, from, to, subject, received_at, body.length).run();
    const id = ins.meta.last_row_id;
    const r2_key = `raw/${id}.txt`;

    await env.EMAILS_R2.put(r2_key, body, {
        httpMetadata: { contentType: "text/plain; charset=utf-8" },
    });
    await env.EVENTS_DB.prepare(
        `UPDATE raw_emails SET r2_key = ? WHERE id = ?`
    ).bind(r2_key, id).run();

    // Fire-and-forget extraction so the caller (or future Email Worker) gets
    // an immediate 200 instead of waiting on Gemini. Cron will sweep any
    // dropped attempts.
    if (env.GEMINI_API_KEY && ctx) {
        ctx.waitUntil(extractEvents(env, id).catch((e) =>
            console.error(`[extract] id=${id} threw:`, e.message)));
    }

    return new Response(JSON.stringify({
        ok: true, id, source_id, r2_key, bytes: body.length,
    }), { headers: { "content-type": "application/json" } });
}

// ─── Family-events extractor (Phase 2) ─────────────────────────────────────
// Reads one raw_emails row + its R2 body, asks Gemini 2.5 Flash for any
// structured calendar events it can find, and INSERTs them into the events
// table. Claims the row up-front with an atomic UPDATE so the per-ingest
// path and the cron sweep can't double-extract; on LLM failure we release
// the claim so cron retries next tick.
//
// Cost: Lakeside likely sends < 50 emails/year. Free tier (Gemini Flash
// has a generous free quota) more than covers this.

const EXTRACT_SCHEMA = {
    type: "OBJECT",
    properties: {
        events: {
            type: "ARRAY",
            items: {
                type: "OBJECT",
                properties: {
                    title:       { type: "STRING" },
                    kind:        { type: "STRING",
                                   enum: ["performance", "field-trip", "deadline",
                                          "meeting", "holiday", "other"] },
                    starts_at:   { type: "STRING",
                                   description: "ISO 8601 with timezone offset; empty string if unknown" },
                    ends_at:     { type: "STRING",
                                   description: "ISO 8601 with timezone offset; empty string if unknown" },
                    location:    { type: "STRING" },
                    description: { type: "STRING" },
                    confidence:  { type: "NUMBER",
                                   description: "0..1 — how sure are you this is a real, dated event" },
                },
                required: ["title", "kind", "confidence"],
            },
        },
    },
    required: ["events"],
};

function buildExtractPrompt(row, body) {
    const today = new Date().toISOString().slice(0, 10);
    return `You extract calendar events from school emails for the parents of a
Lakeside Elementary student (Los Gatos, CA — Pacific Time, currently
${today}). Return ZERO or more events as JSON matching the schema.

For each event:
- title: short, human-friendly (e.g. "Spring Concert")
- kind: pick the closest match from the enum
- starts_at / ends_at: ISO 8601 with Pacific Time offset (e.g.
  "2026-06-07T18:00:00-07:00"). Empty string if not stated.
- location: as written, empty string if not stated
- description: one sentence of context
- confidence: 0..1. Lower when the date is fuzzy or it's not a real event
  (newsletters, reminders without dates, etc.).

If the email has no concrete event (general newsletter, fundraising
appeal, policy notice), return events: [].

FROM: ${row.from_address}
SUBJECT: ${row.subject || "(none)"}

BODY:
${body}`;
}

async function callGemini(env, prompt) {
    const url = `https://generativelanguage.googleapis.com/v1beta/models/`
              + `gemini-2.5-flash:generateContent?key=${env.GEMINI_API_KEY}`;
    const resp = await fetch(url, {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({
            contents: [{ role: "user", parts: [{ text: prompt }] }],
            generationConfig: {
                responseMimeType: "application/json",
                responseSchema: EXTRACT_SCHEMA,
                temperature: 0.1,
            },
        }),
    });
    if (!resp.ok) {
        const text = await resp.text();
        throw new Error(`gemini ${resp.status}: ${text.slice(0, 300)}`);
    }
    const data = await resp.json();
    const jsonText = data.candidates?.[0]?.content?.parts?.[0]?.text;
    if (!jsonText) throw new Error("gemini: no candidate text");
    return JSON.parse(jsonText);
}

// ── Daily note from PAPA (roadmap #5) ───────────────────────────────────────
// Each morning at QUOTE_HOUR in Stanley's timezone, ask the LLM for a short warm
// note and PUBLISH it RETAINED to from-dad. Retain = the watch picks it up
// whenever it next connects, even if it was asleep/offline at 7am. (Trade-off:
// the watch re-receives the retained note on every MQTT reconnect, replaying its
// chime — acceptable for now; dedupe on the watch later if it gets annoying.)
const QUOTE_TZ   = "America/Los_Angeles";   // Stanley's time
const QUOTE_HOUR = 7;                        // 07:00 local

async function generateDailyQuote(env) {
    const today = new Intl.DateTimeFormat("en-US", {
        timeZone: QUOTE_TZ, weekday: "long", month: "long", day: "numeric",
    }).format(new Date());
    const prompt =
`Write a short morning note from a father (Papa) to his 10-year-old son Stanley.
Papa works far away in China; Stanley lives in California. They miss each other.
Today is ${today}. Make it warm and encouraging — courage, kindness, curiosity,
or simply being loved. Plain English, one or two sentences, no quotation marks,
no emoji, no markdown, under 160 characters. End with "— Papa".`;
    const url = `https://generativelanguage.googleapis.com/v1beta/models/`
              + `gemini-2.5-flash:generateContent?key=${env.GEMINI_API_KEY}`;
    const resp = await fetch(url, {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({
            contents: [{ role: "user", parts: [{ text: prompt }] }],
            generationConfig: { temperature: 0.9 },   // variety day to day
        }),
    });
    if (!resp.ok) throw new Error(`gemini ${resp.status}: ${(await resp.text()).slice(0, 200)}`);
    const data = await resp.json();
    const text = data.candidates?.[0]?.content?.parts?.[0]?.text?.trim();
    if (!text) throw new Error("gemini: empty quote");
    return text.replace(/\s+/g, " ").slice(0, 220);   // watch buffer is 240
}

async function sendDailyQuote(env) {
    const text  = await generateDailyQuote(env);
    const topic = `stopwatch/${env.PAIR_ID}/from-dad`;
    await publishMqtt(env.MQTT_BROKER_WSS, topic,
        JSON.stringify({ cmd: "quote", text }), { retain: true });
    console.log("[daily-quote] sent:", text);
}

// ISO 8601 string → unix epoch seconds, or null if blank/unparseable.
function parseIsoToEpoch(s) {
    if (!s) return null;
    const ms = Date.parse(s);
    return Number.isFinite(ms) ? Math.floor(ms / 1000) : null;
}

async function extractEvents(env, raw_email_id) {
    if (!env.GEMINI_API_KEY) return { ok: false, error: "GEMINI_API_KEY not set" };

    // Atomic claim — only one caller succeeds; subsequent calls no-op.
    const claim = await env.EVENTS_DB.prepare(
        `UPDATE raw_emails SET processed = 1 WHERE id = ? AND processed = 0`
    ).bind(raw_email_id).run();
    if (claim.meta.changes === 0) {
        return { ok: true, id: raw_email_id, skipped: "already processed" };
    }

    try {
        const row = await env.EVENTS_DB.prepare(
            `SELECT id, from_address, subject, r2_key FROM raw_emails WHERE id = ?`
        ).bind(raw_email_id).first();
        if (!row) throw new Error("row missing after claim");

        const obj = await env.EMAILS_R2.get(row.r2_key);
        if (!obj) throw new Error(`R2 miss: ${row.r2_key}`);
        const body = await obj.text();

        const result = await callGemini(env, buildExtractPrompt(row, body));
        const events = Array.isArray(result.events) ? result.events : [];

        for (const ev of events) {
            const ins = await env.EVENTS_DB.prepare(
                `INSERT INTO events
                    (raw_email_id, title, kind, starts_at, ends_at,
                     location, description, confidence)
                 VALUES (?, ?, ?, ?, ?, ?, ?, ?)`
            ).bind(
                raw_email_id,
                ev.title,
                ev.kind || null,
                parseIsoToEpoch(ev.starts_at),
                parseIsoToEpoch(ev.ends_at),
                ev.location || null,
                ev.description || null,
                ev.confidence ?? null,
            ).run();
            const event_id = ins.meta.last_row_id;
            // Inline fan-out. Errors don't fail extraction — cron sweep retries.
            try { await pushEventToTelegram(env, event_id); }
            catch (e) { console.error(`[push-tg] event=${event_id} threw:`, e.message); }
            try { await pushEventToMqtt(env, event_id); }
            catch (e) { console.error(`[push-mqtt] event=${event_id} threw:`, e.message); }
        }

        // Placeholder so the admin view shows "looked at, found nothing".
        if (events.length === 0) {
            await env.EVENTS_DB.prepare(
                `INSERT INTO events (raw_email_id, title, kind, confidence)
                 VALUES (?, '(no event)', 'none', 1.0)`
            ).bind(raw_email_id).run();
        }

        console.log(`[extract] id=${raw_email_id} → ${events.length} event(s)`);
        return { ok: true, id: raw_email_id, events_found: events.length };
    } catch (e) {
        // Release the claim so cron retries.
        await env.EVENTS_DB.prepare(
            `UPDATE raw_emails SET processed = 0 WHERE id = ?`
        ).bind(raw_email_id).run();
        throw e;
    }
}

// Sweep up to `limit` unprocessed rows. Called from cron + the manual
// /extract/<token>/run endpoint. Returns per-row results for debugging.
async function extractAllUnprocessed(env, limit = 25) {
    const { results } = await env.EVENTS_DB.prepare(
        `SELECT id FROM raw_emails WHERE processed = 0 ORDER BY id ASC LIMIT ?`
    ).bind(limit).all();

    const out = [];
    for (const r of results) {
        try { out.push(await extractEvents(env, r.id)); }
        catch (e) { out.push({ id: r.id, ok: false, error: e.message }); }
    }
    return out;
}

// ─── Family-events Telegram fan-out (Phase 3) ──────────────────────────────
// For each event we INSERTed, look up subscriptions for its source and send
// one Telegram message per (source, telegram-chat) row. Triggered both
// inline from the extractor and from the cron sweep (catch-up).

function formatEventForTelegram(event) {
    const lines = [`📅 ${event.title}`];
    const meta = [];
    if (event.starts_at) {
        const d = new Date(event.starts_at * 1000);
        const datePart = d.toLocaleString("en-US", {
            timeZone: "America/Los_Angeles",
            weekday: "short", month: "short", day: "numeric",
            hour: "numeric", minute: "2-digit", hour12: true,
        });
        meta.push(`${datePart} PT`);
    }
    if (event.location) meta.push(event.location);
    if (meta.length) lines.push(`   ${meta.join(" · ")}`);
    if (event.source_name) lines.push(`   — ${event.source_name}`);
    if (event.description && event.description !== event.title) {
        lines.push("");
        lines.push(event.description);
    }
    return lines.join("\n");
}

async function pushEventToTelegram(env, event_id) {
    if (!env.TELEGRAM_BOT_TOKEN) {
        return { ok: false, error: "TELEGRAM_BOT_TOKEN not set" };
    }

    // Atomic claim — mirrors the extract pattern.
    const claim = await env.EVENTS_DB.prepare(
        `UPDATE events SET pushed_telegram = 1
            WHERE id = ? AND pushed_telegram = 0`
    ).bind(event_id).run();
    if (claim.meta.changes === 0) {
        return { ok: true, id: event_id, skipped: "already pushed" };
    }

    try {
        const event = await env.EVENTS_DB.prepare(
            `SELECT e.*, r.source_id, s.name AS source_name
                FROM events e
                LEFT JOIN raw_emails r ON e.raw_email_id = r.id
                LEFT JOIN sources    s ON r.source_id    = s.id
                WHERE e.id = ?`
        ).bind(event_id).first();
        if (!event) throw new Error("event not found");

        // Placeholder rows ('(no event)' / kind='none') exist so the admin
        // dashboard knows the email was scanned; we don't broadcast them.
        if (event.kind === "none") {
            return { ok: true, id: event_id, skipped: "placeholder" };
        }

        const { results: subs } = await env.EVENTS_DB.prepare(
            `SELECT channel_id FROM subscriptions
                WHERE source_id    = ?
                  AND channel_kind = 'telegram'
                  AND enabled      = 1`
        ).bind(event.source_id).all();

        if (subs.length === 0) {
            // Nothing to send; keep pushed_telegram=1 so we don't keep
            // rechecking. Subscriptions added later only affect new events.
            return { ok: true, id: event_id, sent: 0, note: "no subscribers" };
        }

        const text = formatEventForTelegram(event);
        let sent = 0;
        for (const sub of subs) {
            const resp = await telegramSend(env.TELEGRAM_BOT_TOKEN,
                                            sub.channel_id, text);
            if (resp.ok) sent++;
            else console.warn(`[push] tg ${resp.status} for chat ${sub.channel_id}`);
        }
        console.log(`[push] event=${event_id} sent=${sent}/${subs.length}`);
        return { ok: true, id: event_id, sent, of: subs.length };
    } catch (e) {
        // Release claim so cron retries.
        await env.EVENTS_DB.prepare(
            `UPDATE events SET pushed_telegram = 0 WHERE id = ?`
        ).bind(event_id).run();
        throw e;
    }
}

async function pushAllUnsynced(env, limit = 50) {
    const { results } = await env.EVENTS_DB.prepare(
        `SELECT id FROM events
            WHERE pushed_telegram = 0
              AND kind != 'none'
            ORDER BY id ASC LIMIT ?`
    ).bind(limit).all();

    const out = [];
    for (const r of results) {
        try { out.push(await pushEventToTelegram(env, r.id)); }
        catch (e) { out.push({ id: r.id, ok: false, error: e.message }); }
    }
    return out;
}

// ─── Family-events MQTT fan-out (Phase 5, bot side) ────────────────────────
// Publishes one retained MQTT message per event so the watch's future
// Schedule app (task #44 firmware-side) can subscribe with a wildcard and
// pick up the full backlog the moment it connects. Topic shape:
//   events/<pair_id>/<event_id>
// `pair_id` comes from the subscription row's channel_id, so the same
// event can fan out to multiple watches by adding more rows.

function buildMqttEventPayload(event) {
    return JSON.stringify({
        id:          event.id,
        title:       event.title,
        kind:        event.kind,
        starts_at:   event.starts_at,    // epoch seconds, may be null
        ends_at:     event.ends_at,
        location:    event.location,
        description: event.description,
        source:      event.source_name,
        confidence:  event.confidence,
    });
}

async function pushEventToMqtt(env, event_id) {
    if (!env.MQTT_BROKER_WSS) {
        return { ok: false, error: "MQTT_BROKER_WSS not set" };
    }

    const claim = await env.EVENTS_DB.prepare(
        `UPDATE events SET pushed_mqtt = 1 WHERE id = ? AND pushed_mqtt = 0`
    ).bind(event_id).run();
    if (claim.meta.changes === 0) {
        return { ok: true, id: event_id, skipped: "already pushed" };
    }

    try {
        const event = await env.EVENTS_DB.prepare(
            `SELECT e.*, r.source_id, s.name AS source_name
                FROM events e
                LEFT JOIN raw_emails r ON e.raw_email_id = r.id
                LEFT JOIN sources    s ON r.source_id    = s.id
                WHERE e.id = ?`
        ).bind(event_id).first();
        if (!event) throw new Error("event not found");
        if (event.kind === "none") {
            return { ok: true, id: event_id, skipped: "placeholder" };
        }

        const { results: subs } = await env.EVENTS_DB.prepare(
            `SELECT channel_id FROM subscriptions
                WHERE source_id    = ?
                  AND channel_kind = 'mqtt'
                  AND enabled      = 1`
        ).bind(event.source_id).all();

        if (subs.length === 0) {
            return { ok: true, id: event_id, sent: 0, note: "no subscribers" };
        }

        const payload = buildMqttEventPayload(event);
        let sent = 0;
        for (const sub of subs) {
            const topic = `events/${sub.channel_id}/${event_id}`;
            try {
                await publishMqtt(env.MQTT_BROKER_WSS, topic, payload,
                                  { retain: true });
                sent++;
            } catch (e) {
                console.error(`[mqtt-push] event=${event_id} topic=${topic} failed:`,
                              e.message);
            }
        }
        console.log(`[mqtt-push] event=${event_id} sent=${sent}/${subs.length}`);
        return { ok: true, id: event_id, sent, of: subs.length };
    } catch (e) {
        await env.EVENTS_DB.prepare(
            `UPDATE events SET pushed_mqtt = 0 WHERE id = ?`
        ).bind(event_id).run();
        throw e;
    }
}

async function pushAllUnsyncedMqtt(env, limit = 50) {
    const { results } = await env.EVENTS_DB.prepare(
        `SELECT id FROM events
            WHERE pushed_mqtt = 0
              AND kind != 'none'
            ORDER BY id ASC LIMIT ?`
    ).bind(limit).all();
    const out = [];
    for (const r of results) {
        try { out.push(await pushEventToMqtt(env, r.id)); }
        catch (e) { out.push({ id: r.id, ok: false, error: e.message }); }
    }
    return out;
}

// ─── Family-events .ics export (Phase 6) ───────────────────────────────────
// Serves RFC 5545 calendar feeds so Apple Calendar / Google Calendar can
// subscribe to the URL and refresh automatically. One feed for everything
// (/ics/<token>/all.ics) plus per-source feeds (/ics/<token>/<source>.ics)
// so the user can selectively share Lakeside-only with the kid's grandparents
// etc.

// Escape per RFC 5545 §3.3.11 (TEXT property value): backslash, comma,
// semicolon, newline. Order matters — backslash first.
function icsEscape(s) {
    return String(s)
        .replace(/\\/g, "\\\\")
        .replace(/\n/g, "\\n")
        .replace(/,/g,  "\\,")
        .replace(/;/g,  "\\;");
}

// epoch → "20260608T010000Z" (UTC, basic format per ICS).
function icsUtcStamp(epoch) {
    const d = new Date(epoch * 1000);
    const p = (n) => String(n).padStart(2, "0");
    return `${d.getUTCFullYear()}${p(d.getUTCMonth() + 1)}${p(d.getUTCDate())}`
         + `T${p(d.getUTCHours())}${p(d.getUTCMinutes())}${p(d.getUTCSeconds())}Z`;
}

// Build VCALENDAR text from a list of event rows (joined with source name).
function eventsToIcs(events, calendarName = "Papa Watch — Family Events") {
    const now = icsUtcStamp(Math.floor(Date.now() / 1000));
    const lines = [
        "BEGIN:VCALENDAR",
        "VERSION:2.0",
        "PRODID:-//papa-watch//family-events//EN",
        "CALSCALE:GREGORIAN",
        "METHOD:PUBLISH",
        `X-WR-CALNAME:${icsEscape(calendarName)}`,
    ];

    for (const e of events) {
        // ICS requires DTSTART; events without a time we skip rather than
        // emit malformed entries.
        if (!e.starts_at) continue;
        lines.push("BEGIN:VEVENT");
        lines.push(`UID:papa-event-${e.id}@papa-watch-bot`);
        lines.push(`DTSTAMP:${now}`);
        lines.push(`DTSTART:${icsUtcStamp(e.starts_at)}`);
        if (e.ends_at) lines.push(`DTEND:${icsUtcStamp(e.ends_at)}`);
        const title = e.source_name ? `[${e.source_name}] ${e.title}` : e.title;
        lines.push(`SUMMARY:${icsEscape(title)}`);
        if (e.location) lines.push(`LOCATION:${icsEscape(e.location)}`);
        if (e.description) lines.push(`DESCRIPTION:${icsEscape(e.description)}`);
        lines.push("END:VEVENT");
    }

    lines.push("END:VCALENDAR");
    // RFC 5545 mandates CRLF line endings.
    return lines.join("\r\n") + "\r\n";
}

// ─── Family-events admin web UI (Phase 7) ──────────────────────────────────
// One read-only page at /admin/<INGEST_TOKEN>/ that shows the current state
// of raw_emails / events / subscriptions, plus a "view body" link that
// streams an R2 object back as text. Editing UI (toggle subscription
// enabled, re-extract a row) can come later when there's a need.

function htmlEscape(s) {
    return String(s ?? "")
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;")
        .replace(/"/g, "&quot;");
}

function fmtEpoch(epoch, tz) {
    if (!epoch) return "—";
    return new Date(epoch * 1000).toLocaleString("en-US", {
        timeZone: tz,
        month: "short", day: "numeric", year: "numeric",
        hour: "2-digit", minute: "2-digit", hour12: false,
    });
}

function fmtBool(v, trueLabel = "yes", falseLabel = "no") {
    return v ? `<span class="ok">${trueLabel}</span>`
             : `<span class="muted">${falseLabel}</span>`;
}

async function renderAdminPage(env, token) {
    const tz = "America/Los_Angeles";   // PAPA spends most days in CN but the
                                        // emails are PT-local; show in PT.

    const [stats, sources, rawEmails, events, subs] = await Promise.all([
        env.EVENTS_DB.prepare(`
            SELECT
                (SELECT COUNT(*) FROM raw_emails)    AS raw_emails,
                (SELECT COUNT(*) FROM events
                    WHERE kind != 'none')            AS events,
                (SELECT COUNT(*) FROM subscriptions
                    WHERE enabled = 1)               AS subscriptions,
                (SELECT COUNT(*) FROM sources)       AS sources
        `).first(),
        env.EVENTS_DB.prepare(
            `SELECT id, name, kind, email_match FROM sources ORDER BY id`
        ).all(),
        env.EVENTS_DB.prepare(
            `SELECT id, source_id, from_address, subject, received_at,
                    bytes, processed
                FROM raw_emails ORDER BY id DESC LIMIT 25`
        ).all(),
        env.EVENTS_DB.prepare(
            `SELECT e.id, e.raw_email_id, e.title, e.kind,
                    e.starts_at, e.location, ROUND(e.confidence,2) AS conf,
                    e.pushed_telegram, e.pushed_mqtt,
                    s.name AS source_name
                FROM events e
                LEFT JOIN raw_emails r ON e.raw_email_id = r.id
                LEFT JOIN sources    s ON r.source_id    = s.id
                ORDER BY e.id DESC LIMIT 50`
        ).all(),
        env.EVENTS_DB.prepare(
            `SELECT sub.id, sub.source_id, src.name AS source_name,
                    sub.channel_kind, sub.channel_id, sub.enabled
                FROM subscriptions sub
                LEFT JOIN sources src ON sub.source_id = src.id
                ORDER BY sub.id`
        ).all(),
    ]);

    const rawTable = rawEmails.results.map((r) => `
        <tr>
          <td>${r.id}</td>
          <td><code>${htmlEscape(r.from_address)}</code></td>
          <td>${htmlEscape(r.subject ?? "(no subject)")}</td>
          <td class="mono">${fmtEpoch(r.received_at, tz)}</td>
          <td class="mono">${r.bytes ?? "—"}</td>
          <td>${fmtBool(r.processed, "✓", "·")}</td>
          <td><a href="/admin/${token}/raw/${r.id}">view</a></td>
        </tr>`).join("");

    const eventsTable = events.results.map((e) => `
        <tr>
          <td>${e.id}</td>
          <td>${htmlEscape(e.source_name ?? "—")}</td>
          <td>${htmlEscape(e.title)}</td>
          <td class="mono">${htmlEscape(e.kind ?? "—")}</td>
          <td class="mono">${fmtEpoch(e.starts_at, tz)}</td>
          <td>${htmlEscape(e.location ?? "—")}</td>
          <td class="mono">${e.conf ?? "—"}</td>
          <td>${fmtBool(e.pushed_telegram, "tg", "—")}
              ${fmtBool(e.pushed_mqtt, "mqtt", "—")}</td>
        </tr>`).join("");

    const subsTable = subs.results.map((s) => `
        <tr>
          <td>${s.id}</td>
          <td>${htmlEscape(s.source_name ?? "—")}</td>
          <td class="mono">${htmlEscape(s.channel_kind)}</td>
          <td class="mono">${htmlEscape(s.channel_id)}</td>
          <td>${fmtBool(s.enabled, "on", "off")}</td>
        </tr>`).join("");

    const sourcesTable = sources.results.map((s) => `
        <tr>
          <td>${s.id}</td>
          <td>${htmlEscape(s.name)}</td>
          <td class="mono">${htmlEscape(s.kind)}</td>
          <td class="mono">${htmlEscape(s.email_match ?? "—")}</td>
        </tr>`).join("");

    return `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>papa-watch · family events</title>
<style>
  :root { --ink:#1a1a1a; --muted:#888; --line:#eee; --bg:#fdfdfa; --ok:#1f8a4c; }
  body { font:14px/1.5 -apple-system,BlinkMacSystemFont,sans-serif;
         max-width:1180px; margin:24px auto; padding:0 20px;
         color:var(--ink); background:var(--bg); }
  h1 { font-size:22px; margin:0 0 4px; }
  h1 small { font-weight:400; color:var(--muted); font-size:13px; }
  h2 { font-size:14px; text-transform:uppercase; letter-spacing:0.08em;
       color:var(--muted); margin:32px 0 8px;
       border-bottom:1px solid var(--line); padding-bottom:4px; }
  .stats { margin:12px 0 4px; }
  .stat { display:inline-block; margin-right:32px; }
  .stat b { font-size:22px; display:block; }
  table { border-collapse:collapse; width:100%; margin:6px 0 4px; }
  th,td { border-bottom:1px solid var(--line); padding:6px 10px;
          text-align:left; vertical-align:top; font-size:13px; }
  th { color:var(--muted); font-weight:500; font-size:11px;
       text-transform:uppercase; letter-spacing:0.06em; }
  tr:hover td { background:#fff5e6; }
  code,.mono { font:12px/1.4 ui-monospace,"SF Mono",monospace; color:#444; }
  .ok { color:var(--ok); font-weight:600; }
  .muted { color:var(--muted); }
  a { color:#1064c2; text-decoration:none; }
  a:hover { text-decoration:underline; }
</style>
</head>
<body>
<h1>papa-watch · family events <small>admin</small></h1>

<div class="stats">
  <span class="stat"><b>${stats?.raw_emails ?? 0}</b>raw emails</span>
  <span class="stat"><b>${stats?.events ?? 0}</b>events</span>
  <span class="stat"><b>${stats?.subscriptions ?? 0}</b>subscriptions</span>
  <span class="stat"><b>${stats?.sources ?? 0}</b>sources</span>
</div>

<h2>Sources</h2>
<table><thead><tr><th>id</th><th>name</th><th>kind</th><th>email match</th></tr></thead>
<tbody>${sourcesTable || `<tr><td colspan="4" class="muted">none</td></tr>`}</tbody></table>

<h2>Subscriptions</h2>
<table><thead><tr><th>id</th><th>source</th><th>channel</th><th>id</th><th>enabled</th></tr></thead>
<tbody>${subsTable || `<tr><td colspan="5" class="muted">none</td></tr>`}</tbody></table>

<h2>Events (latest 50)</h2>
<table><thead><tr><th>id</th><th>source</th><th>title</th><th>kind</th><th>starts (PT)</th><th>location</th><th>conf</th><th>pushed</th></tr></thead>
<tbody>${eventsTable || `<tr><td colspan="8" class="muted">none</td></tr>`}</tbody></table>

<h2>Recent raw emails (latest 25)</h2>
<table><thead><tr><th>id</th><th>from</th><th>subject</th><th>received (PT)</th><th>bytes</th><th>extracted</th><th>body</th></tr></thead>
<tbody>${rawTable || `<tr><td colspan="7" class="muted">none</td></tr>`}</tbody></table>

</body></html>`;
}

// `which` is either "all" or a source id (numeric string).
async function buildIcsFeed(env, which) {
    let rows;
    if (which === "all") {
        rows = await env.EVENTS_DB.prepare(
            `SELECT e.id, e.title, e.starts_at, e.ends_at, e.location,
                    e.description, s.name AS source_name
                FROM events e
                LEFT JOIN raw_emails r ON e.raw_email_id = r.id
                LEFT JOIN sources    s ON r.source_id    = s.id
                WHERE e.kind != 'none'
                ORDER BY e.starts_at ASC`
        ).all();
    } else {
        const sid = parseInt(which, 10);
        if (!Number.isFinite(sid)) return null;
        rows = await env.EVENTS_DB.prepare(
            `SELECT e.id, e.title, e.starts_at, e.ends_at, e.location,
                    e.description, s.name AS source_name
                FROM events e
                LEFT JOIN raw_emails r ON e.raw_email_id = r.id
                LEFT JOIN sources    s ON r.source_id    = s.id
                WHERE r.source_id = ? AND e.kind != 'none'
                ORDER BY e.starts_at ASC`
        ).bind(sid).all();
    }
    return eventsToIcs(rows.results || []);
}

// ─── Durable Object: HeartRelay ────────────────────────────────────────────
// Stanley's watch publishes to `stopwatch/<pair>/from-son` over MQTT.
// A stateless Worker can't subscribe long-lived, so we park a Durable Object
// that holds the MQTT-over-WSS socket open and forwards inbound PUBLISH
// packets to Telegram. A Cron Trigger pokes us every minute as a backstop in
// case the DO is evicted; the DO also self-schedules an alarm so it survives
// independently of the cron.
export class HeartRelay {
    constructor(state, env) {
        this.state = state;
        this.env = env;
        this.ws = null;
        this.connected = false;
        this.subscribed = false;
        this.pingTimer = null;
        this.bootedAt = 0;
        this.lastForwardAt = 0;
        this.lastError = null;
    }

    async fetch(request) {
        const url = new URL(request.url);
        if (url.pathname === "/poke") {
            await this.ensureConnected();
            return new Response("ok");
        }
        if (url.pathname === "/status") {
            return new Response(JSON.stringify({
                connected: this.connected,
                subscribed: this.subscribed,
                bootedAt: this.bootedAt,
                uptimeMs: this.bootedAt ? Date.now() - this.bootedAt : 0,
                lastForwardAt: this.lastForwardAt,
                lastError: this.lastError,
            }, null, 2), { headers: { "content-type": "application/json" } });
        }
        return new Response("not found", { status: 404 });
    }

    async alarm() {
        await this.ensureConnected();
        // Heartbeat: re-check every 60s. If WS still alive this is a cheap noop.
        await this.state.storage.setAlarm(Date.now() + 60_000);
    }

    async ensureConnected() {
        if (this.connected && this.ws) return;
        try {
            await this.connect();
        } catch (e) {
            this.lastError = e.message;
            console.error("[relay] connect failed:", e.message);
        }
        // Always rearm — alarm survives even if connect threw.
        await this.state.storage.setAlarm(Date.now() + 60_000);
    }

    async connect() {
        console.log("[relay] connecting to", this.env.MQTT_BROKER_WSS);
        const response = await fetch(this.env.MQTT_BROKER_WSS.replace(/^wss:/, "https:"), {
            headers: { Upgrade: "websocket", "Sec-WebSocket-Protocol": "mqtt" },
        });
        const ws = response.webSocket;
        if (!ws) throw new Error(`broker did not upgrade: ${response.status}`);
        ws.accept();
        this.ws = ws;
        this.bootedAt = Date.now();
        this.lastError = null;

        const clientId = `papa-relay-${Date.now().toString(36)}`;
        ws.send(makeConnect(clientId));

        ws.addEventListener("message", async (ev) => {
            try {
                const buf = ev.data instanceof ArrayBuffer
                    ? new Uint8Array(ev.data)
                    : new Uint8Array(await ev.data.arrayBuffer());
                await this.onPacket(buf);
            } catch (e) {
                console.error("[relay] onPacket threw:", e.message);
            }
        });
        ws.addEventListener("close", () => {
            console.log("[relay] ws closed");
            this.connected = false;
            this.subscribed = false;
            if (this.pingTimer) { clearInterval(this.pingTimer); this.pingTimer = null; }
            this.ws = null;
        });
        ws.addEventListener("error", (e) => {
            this.lastError = "ws error: " + (e?.message || "");
            console.error("[relay]", this.lastError);
        });
    }

    async onPacket(buf) {
        const type = buf[0] & 0xf0;
        if (type === 0x20) {                          // CONNACK
            console.log("[relay] CONNACK");
            this.connected = true;
            const topic = `stopwatch/${this.env.PAIR_ID}/from-son`;
            this.ws.send(makeSubscribe(1, topic));
            // KEEPALIVE is 60s; broker drops us if no traffic. Ping every 40s.
            if (this.pingTimer) clearInterval(this.pingTimer);
            this.pingTimer = setInterval(() => {
                try { this.ws?.send(PINGREQ_PACKET); } catch (_) {}
            }, 40_000);
        } else if (type === 0x90) {                   // SUBACK
            console.log("[relay] SUBACK");
            this.subscribed = true;
        } else if (type === 0x30) {                   // PUBLISH
            const pkt = parsePublishPacket(buf);
            if (pkt) {
                console.log(`[relay] PUB ${pkt.topic} ${pkt.payload}`);
                await this.forwardToTelegram(pkt);
                this.lastForwardAt = Date.now();
            }
        }
        // PINGRESP (0xd0) is ignored.
    }

    async forwardToTelegram({ payload }) {
        let data;
        try { data = JSON.parse(payload); } catch (_) { data = { raw: payload }; }
        const t    = data.t || Math.floor(Date.now() / 1000);
        const note = data.note || data.emoji || "❤";
        // Stanley's wall-clock time on the watch in PT.
        const hhmm = new Date(t * 1000).toLocaleTimeString("en-US", {
            hour: "2-digit", minute: "2-digit", timeZone: "America/Los_Angeles", hour12: false,
        });
        const text = `💗 Stanley sent ${note}  ·  ${hhmm} PT`;
        await telegramSend(this.env.TELEGRAM_BOT_TOKEN, this.env.PAPA_CHAT_ID, text);
    }
}

// Single named instance — there's only ever one relay for our pair.
function getRelay(env) {
    const id = env.HEART_RELAY.idFromName(env.PAIR_ID);
    return env.HEART_RELAY.get(id);
}

export default {
    async fetch(request, env, ctx) {
        const url = new URL(request.url);
        console.log(`[req] ${request.method} ${url.pathname}`);

        // Health check / root
        if (request.method === "GET" && url.pathname === "/") {
            return new Response("papa-watch-bot up", { status: 200 });
        }

        // Relay status — handy for `curl https://<worker>/relay/status`.
        if (request.method === "GET" && url.pathname === "/relay/status") {
            return getRelay(env).fetch("https://do/status");
        }

        // Manual poke — same thing cron does, useful for first-time bring-up.
        if (request.method === "GET" && url.pathname === "/relay/poke") {
            return getRelay(env).fetch("https://do/poke");
        }

        // Family-events ingest. Phase 1: HTTP-only — Cloudflare Email
        // Routing isn't wired up yet, so callers (curl, Gmail filter via
        // a forwarder, or future Email Worker) POST here directly. Path
        // embeds INGEST_TOKEN so random scanners can't spam D1.
        //   curl -X POST https://<worker>/ingest/<token>/email \
        //        -H "content-type: application/json" \
        //        -d '{"from":"...","to":"...","subject":"...","body":"..."}'
        if (request.method === "POST" &&
            url.pathname === `/ingest/${env.INGEST_TOKEN}/email`) {
            return handleIngestEmail(request, env, ctx).catch((e) => {
                console.error("[ingest] threw:", e.message);
                return new Response(`ingest failed: ${e.message}`, { status: 500 });
            });
        }

        // Manual extractor sweep — sweeps `processed = 0` raw_emails through
        // Gemini. Same token as ingest. Useful after rotating GEMINI_API_KEY
        // or for catching up after a cold backlog.
        if (request.method === "GET" &&
            url.pathname === `/extract/${env.INGEST_TOKEN}/run`) {
            const limit = parseInt(url.searchParams.get("limit") || "25", 10);
            const results = await extractAllUnprocessed(env, limit);
            return new Response(JSON.stringify({ ok: true, results }, null, 2),
                { headers: { "content-type": "application/json" } });
        }

        // Manual daily-note trigger — generates + publishes a PAPA note right
        // now (same as the 7am cron), so we can test end-to-end on demand.
        if (request.method === "GET" &&
            url.pathname === `/quote/${env.INGEST_TOKEN}/run`) {
            const text = await generateDailyQuote(env);
            const topic = `stopwatch/${env.PAIR_ID}/from-dad`;
            await publishMqtt(env.MQTT_BROKER_WSS, topic,
                JSON.stringify({ cmd: "quote", text }), { retain: true });
            return new Response(JSON.stringify({ ok: true, text }, null, 2),
                { headers: { "content-type": "application/json" } });
        }

        // Manual Telegram fan-out sweep — pushes all events with
        // pushed_telegram=0. Useful after seeding a new subscription or
        // recovering from a Telegram outage.
        if (request.method === "GET" &&
            url.pathname === `/push/${env.INGEST_TOKEN}/run`) {
            const limit = parseInt(url.searchParams.get("limit") || "50", 10);
            const results = await pushAllUnsynced(env, limit);
            return new Response(JSON.stringify({ ok: true, results }, null, 2),
                { headers: { "content-type": "application/json" } });
        }
        if (request.method === "GET" &&
            url.pathname === `/push-mqtt/${env.INGEST_TOKEN}/run`) {
            const limit = parseInt(url.searchParams.get("limit") || "50", 10);
            const results = await pushAllUnsyncedMqtt(env, limit);
            return new Response(JSON.stringify({ ok: true, results }, null, 2),
                { headers: { "content-type": "application/json" } });
        }

        // Admin web UI — read-only overview of raw_emails / events /
        // subscriptions. Token-gated path so accidental URL leaks aren't
        // catastrophic. Edit actions can come later.
        if (request.method === "GET" &&
            url.pathname === `/admin/${env.INGEST_TOKEN}/`) {
            const html = await renderAdminPage(env, env.INGEST_TOKEN);
            return new Response(html, {
                headers: { "content-type": "text/html; charset=utf-8" },
            });
        }
        // Companion endpoint: stream a raw email body from R2.
        {
            const m = url.pathname.match(
                /^\/admin\/([^\/]+)\/raw\/(\d+)$/);
            if (request.method === "GET" && m && m[1] === env.INGEST_TOKEN) {
                const row = await env.EVENTS_DB.prepare(
                    `SELECT r2_key, from_address, subject FROM raw_emails WHERE id = ?`
                ).bind(parseInt(m[2], 10)).first();
                if (!row) return new Response("not found", { status: 404 });
                const obj = await env.EMAILS_R2.get(row.r2_key);
                if (!obj) return new Response("R2 miss", { status: 404 });
                const header = `FROM: ${row.from_address}\n`
                             + `SUBJECT: ${row.subject ?? "(none)"}\n`
                             + `KEY: ${row.r2_key}\n\n`;
                const body = header + (await obj.text());
                return new Response(body, {
                    headers: { "content-type": "text/plain; charset=utf-8" },
                });
            }
        }

        // .ics calendar feed for Apple/Google Calendar URL subscription.
        //   /ics/<INGEST_TOKEN>/all.ics      — every source
        //   /ics/<INGEST_TOKEN>/<sourceId>.ics — one source (e.g. 1.ics = Lakeside)
        // Calendar apps poll on their own cadence (15-30 min typically).
        {
            const m = url.pathname.match(
                /^\/ics\/([^\/]+)\/([^\/]+)\.ics$/);
            if (request.method === "GET" && m && m[1] === env.INGEST_TOKEN) {
                const body = await buildIcsFeed(env, m[2]);
                if (body === null) return new Response("bad source", { status: 400 });
                return new Response(body, {
                    headers: {
                        "content-type": "text/calendar; charset=utf-8",
                        "cache-control": "public, max-age=300",
                    },
                });
            }
        }

        // Telegram webhook. Path includes the WEBHOOK_SECRET so unauthenticated
        // posters can't trigger it. Telegram also echoes a header for additional
        // verification (set via setWebhook's secret_token field).
        if (request.method === "POST" && url.pathname === `/tg/${env.WEBHOOK_SECRET}`) {
            const headerSecret = request.headers.get("X-Telegram-Bot-Api-Secret-Token");
            if (headerSecret !== env.WEBHOOK_SECRET) {
                return new Response("forbidden", { status: 403 });
            }
            const update = await request.json();
            // Telegram expects fast 200; do the work asynchronously.
            ctx.waitUntil(handleTelegram(update, env, url.origin).catch((e) =>
                console.error("[handleTelegram] threw:", e.message, e.stack)));
            return new Response("ok");
        }

        // Stanley→PAPA voice (roadmap #6): the watch POSTs raw 16 kHz mono int16
        // PCM here; we wrap it as WAV and drop it into PAPA's Telegram chat.
        if (request.method === "POST" && url.pathname === `/voice/${env.VOICE_TOKEN}/upload`) {
            try {
                const pcm = new Uint8Array(await request.arrayBuffer());
                if (pcm.length < 2 || pcm.length > 4 * 1024 * 1024)
                    return new Response("bad size", { status: 400 });
                const secs = (pcm.length / 2 / 16000).toFixed(1);
                await tgSendAudioWav(env.TELEGRAM_BOT_TOKEN, env.PAPA_CHAT_ID,
                    pcm16ToWav(pcm), `🎙️ Stanley · ${secs}s`);
                return new Response("ok");
            } catch (e) {
                console.error("[voice upload] failed:", e.stack || e.message);
                return new Response(`fail: ${e.message}`, { status: 500 });
            }
        }

        // The watch's voice inbox: the last week of PAPA clips, newest first.
        if (request.method === "GET" && url.pathname === `/voice/${env.VOICE_TOKEN}/inbox`) {
            const listed = await env.EMAILS_R2.list({ prefix: "voice/", include: ["customMetadata"] });
            const items = listed.objects.map((o) => {
                const id = o.key.slice("voice/".length).replace(/\.pcm$/, "");
                const ts = +(o.customMetadata?.ts) || Math.floor(o.uploaded.getTime() / 1000);
                const secs = o.customMetadata?.secs ? +o.customMetadata.secs
                                                    : +(o.size / 2 / 16000).toFixed(1);
                return { id, ts, secs };
            }).sort((a, b) => b.ts - a.ts).slice(0, 20);
            return new Response(JSON.stringify(items), { headers: { "content-type": "application/json" } });
        }

        // Serve a decoded PAPA voice clip to the watch (raw int16 PCM). The watch
        // reads Content-Length then streams the body into a PSRAM buffer.
        {
            const prefix = `/voice/${env.VOICE_TOKEN}/clip/`;
            if (request.method === "GET" && url.pathname.startsWith(prefix)) {
                const id = url.pathname.slice(prefix.length).replace(/\.pcm$/, "");
                const obj = await env.EMAILS_R2.get(`voice/${id}.pcm`);
                if (!obj) return new Response("not found", { status: 404 });
                return new Response(obj.body, {
                    headers: {
                        "content-type": "application/octet-stream",
                        "content-length": String(obj.size),
                    },
                });
            }
        }

        return new Response("not found", { status: 404 });
    },

    // Cron Trigger — pokes the relay so the DO stays loaded + reconnects on
    // drop, and sweeps any unprocessed raw_emails through the LLM extractor.
    // The extractor's first step is a cheap D1 SELECT, so when there's no
    // work it costs essentially nothing (no Gemini call).
    async scheduled(event, env, ctx) {
        ctx.waitUntil(getRelay(env).fetch("https://do/poke").catch((e) =>
            console.error("[cron] poke failed:", e.message)));

        // Daily note from PAPA — fire once, at QUOTE_HOUR:00 in Stanley's TZ.
        // (No persistence: a skipped cron minute = missed day, fine for a gift.
        // The retained publish covers the watch being offline at send time.)
        if (env.GEMINI_API_KEY && env.MQTT_BROKER_WSS && env.PAIR_ID) {
            const parts = new Intl.DateTimeFormat("en-US", {
                timeZone: QUOTE_TZ, hour: "2-digit", minute: "2-digit", hour12: false,
            }).formatToParts(new Date(event.scheduledTime));
            const h = +parts.find((p) => p.type === "hour").value;
            const m = +parts.find((p) => p.type === "minute").value;
            if (h === QUOTE_HOUR && m === 0) {
                ctx.waitUntil(sendDailyQuote(env).catch((e) =>
                    console.error("[cron] daily-quote failed:", e.message)));
            }
        }

        if (env.GEMINI_API_KEY && env.EVENTS_DB) {
            ctx.waitUntil(extractAllUnprocessed(env).catch((e) =>
                console.error("[cron] extract sweep failed:", e.message)));
        }
        if (env.TELEGRAM_BOT_TOKEN && env.EVENTS_DB) {
            ctx.waitUntil(pushAllUnsynced(env).catch((e) =>
                console.error("[cron] tg push sweep failed:", e.message)));
        }
        if (env.MQTT_BROKER_WSS && env.EVENTS_DB) {
            ctx.waitUntil(pushAllUnsyncedMqtt(env).catch((e) =>
                console.error("[cron] mqtt push sweep failed:", e.message)));
        }
        if (env.EMAILS_R2) {
            ctx.waitUntil(pruneVoiceInbox(env, event.scheduledTime).catch((e) =>
                console.error("[cron] voice prune failed:", e.message)));
        }
    },
};

// The PAPA voice inbox is a rolling 7-day window — drop clips older than that.
async function pruneVoiceInbox(env, nowMs) {
    const cutoff = Math.floor(nowMs / 1000) - 7 * 86400;
    const listed = await env.EMAILS_R2.list({ prefix: "voice/", include: ["customMetadata"] });
    for (const o of listed.objects) {
        const ts = +(o.customMetadata?.ts) || Math.floor(o.uploaded.getTime() / 1000);
        if (ts < cutoff) await env.EMAILS_R2.delete(o.key);
    }
}
