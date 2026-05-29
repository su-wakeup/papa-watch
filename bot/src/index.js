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

const DISCONNECT_PACKET = new Uint8Array([0xe0, 0x00]);

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
        const timeout = setTimeout(() => reject(new Error("CONNACK timeout")), 5000);

        ws.addEventListener("message", (ev) => {
            const data = ev.data instanceof ArrayBuffer ? new Uint8Array(ev.data) : ev.data;
            if (data[0] === 0x20 /* CONNACK */) {
                clearTimeout(timeout);
                ws.send(makePublish(topic, payload));
                setTimeout(() => {
                    ws.send(DISCONNECT_PACKET);
                    ws.close();
                    resolve();
                }, 80);
            }
        });
        ws.addEventListener("error", (e) => { clearTimeout(timeout); reject(e); });
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
    "/ota             trigger firmware update check",
    "/help            this message",
].join("\n");

async function handleTelegram(update, env) {
    const msg = update.message;
    if (!msg || !msg.text) return new Response("ok");

    const text = msg.text.trim();
    const from = msg.from?.username || msg.from?.first_name || "unknown";
    const chatId = msg.chat.id;

    // optional allowlist
    if (env.ALLOWED_USERS) {
        const allowed = env.ALLOWED_USERS.split(",").map((s) => s.trim()).filter(Boolean);
        if (allowed.length && !allowed.includes(from)) {
            await telegramSend(env.TELEGRAM_BOT_TOKEN, chatId, `not authorized: @${from}`);
            return new Response("ok");
        }
    }

    const topic = `stopwatch/${env.PAIR_ID}/from-dad`;

    if (text === "/start" || text === "/help") {
        await telegramSend(env.TELEGRAM_BOT_TOKEN, chatId, HELP);
        return new Response("ok");
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
            await telegramSend(env.TELEGRAM_BOT_TOKEN, chatId, `failed: ${e.message}`);
        }
        return new Response("ok");
    }

    if (text === "/ota") {
        try {
            await publishMqtt(env.MQTT_BROKER_WSS, topic, JSON.stringify({ cmd: "ota" }));
            await telegramSend(env.TELEGRAM_BOT_TOKEN, chatId, "⚙️ OTA check triggered");
        } catch (e) {
            await telegramSend(env.TELEGRAM_BOT_TOKEN, chatId, `failed: ${e.message}`);
        }
        return new Response("ok");
    }

    await telegramSend(env.TELEGRAM_BOT_TOKEN, chatId, `unknown: ${text}\n${HELP}`);
    return new Response("ok");
}

export default {
    async fetch(request, env, ctx) {
        const url = new URL(request.url);

        // Health check / root
        if (request.method === "GET" && url.pathname === "/") {
            return new Response("papa-watch-bot up", { status: 200 });
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
            ctx.waitUntil(handleTelegram(update, env));
            return new Response("ok");
        }

        return new Response("not found", { status: 404 });
    },
};
