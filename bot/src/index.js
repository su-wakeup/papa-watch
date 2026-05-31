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

    // Cron Trigger — pokes the relay so the DO stays loaded + reconnects on drop.
    async scheduled(event, env, ctx) {
        ctx.waitUntil(getRelay(env).fetch("https://do/poke").catch((e) =>
            console.error("[cron] poke failed:", e.message)));
    },
};
