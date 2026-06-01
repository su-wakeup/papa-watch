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

function makePublish(topic, payload) {
    const topicEnc = encodeUtf8Prefixed(topic);
    const payloadBytes = typeof payload === "string" ? new TextEncoder().encode(payload) : payload;
    const body = concatBytes(topicEnc, payloadBytes);
    return concatBytes(new Uint8Array([0x30]), encodeRemainingLength(body.length), body);
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

async function publishMqtt(brokerWss, topic, payload) {
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
                ws.send(makePublish(topic, payload));
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
    "/ota             trigger firmware update check",
    "/help            this message",
].join("\n");

async function handleTelegram(update, env) {
    const msg = update.message;
    if (!msg || !msg.text) return;

    const text = msg.text.trim();
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

    // Resolve source by email_match suffix. NULL = unknown sender; row still
    // gets stored so we can backfill source_id later once we add the match.
    const source = await env.EVENTS_DB.prepare(
        `SELECT id FROM sources
            WHERE email_match IS NOT NULL
              AND ? LIKE '%' || email_match
            LIMIT 1`
    ).bind(from).first();
    const source_id = source?.id ?? null;

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
            catch (e) { console.error(`[push] event=${event_id} threw:`, e.message); }
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
            ctx.waitUntil(handleTelegram(update, env).catch((e) =>
                console.error("[handleTelegram] threw:", e.message, e.stack)));
            return new Response("ok");
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
        if (env.GEMINI_API_KEY && env.EVENTS_DB) {
            ctx.waitUntil(extractAllUnprocessed(env).catch((e) =>
                console.error("[cron] extract sweep failed:", e.message)));
        }
        if (env.TELEGRAM_BOT_TOKEN && env.EVENTS_DB) {
            ctx.waitUntil(pushAllUnsynced(env).catch((e) =>
                console.error("[cron] push sweep failed:", e.message)));
        }
    },
};
