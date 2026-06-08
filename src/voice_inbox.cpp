#include "voice_inbox.h"
#include "voice_play.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

namespace voice_inbox {

// Same Worker + token the record leg uses (obscurity, not a secret).
static const char* BASE =
    "https://papa-watch-bot.su-wakeup.workers.dev/voice/555dcb03d94bd5e8";

static constexpr int MAX = 20;
static Item     s_items[MAX];
static int      s_count      = 0;
static uint32_t s_alerted_ts = 0;     // newest ts we've already buzzed for

void begin() {
    Preferences p;
    p.begin("voice", true);
    s_alerted_ts = p.getUInt("alerted", 0);
    p.end();
}

bool onPush(uint32_t ts) {
    if (ts <= s_alerted_ts) return false;     // already buzzed (retained re-delivery)
    s_alerted_ts = ts;
    Preferences p;
    p.begin("voice", false);
    p.putUInt("alerted", ts);
    p.end();
    return true;
}

void fetch() {
    if (WiFi.status() != WL_CONNECTED) return;
    char url[160];
    snprintf(url, sizeof(url), "%s/inbox", BASE);

    HTTPClient http;
    WiFiClientSecure secure;
    secure.setInsecure();                     // skip cert check (same as OTA)
    if (!http.begin(secure, url)) return;
    http.setTimeout(4000);
    int code = http.GET();
    if (code != HTTP_CODE_OK) { http.end(); return; }
    String body = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, body)) return;
    s_count = 0;
    for (JsonObject o : doc.as<JsonArray>()) {
        if (s_count >= MAX) break;
        Item& it = s_items[s_count];
        strncpy(it.id, o["id"] | "", sizeof(it.id) - 1);
        it.id[sizeof(it.id) - 1] = 0;
        it.ts   = o["ts"]   | 0u;
        it.secs = o["secs"] | 0.0f;
        if (it.id[0]) s_count++;
    }
}

int count() { return s_count; }
const Item& item(int i) { return s_items[i]; }

void play(int i) {
    if (i < 0 || i >= s_count) return;
    char url[200];
    snprintf(url, sizeof(url), "%s/clip/%s.pcm", BASE, s_items[i].id);
    voice_play::request(url);
}

}  // namespace voice_inbox
