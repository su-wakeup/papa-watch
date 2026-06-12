#include "note_inbox.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <string.h>

namespace note_inbox {

// Same Worker + token the voice legs use.
static const char* BASE =
    "https://papa-watch-bot.su-wakeup.workers.dev/voice/555dcb03d94bd5e8";

static constexpr int MAX = 15;
static Item s_items[MAX];
static int  s_count = 0;

void fetch() {
    if (WiFi.status() != WL_CONNECTED) return;
    char url[160];
    snprintf(url, sizeof(url), "%s/notes", BASE);

    HTTPClient http;
    WiFiClientSecure secure;
    secure.setInsecure();
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
        it.ts = o["ts"] | 0u;
        strncpy(it.text, o["text"] | "", sizeof(it.text) - 1);
        it.text[sizeof(it.text) - 1] = 0;
        if (it.text[0]) s_count++;
    }
}

int count() { return s_count; }
const Item& item(int i) { return s_items[i]; }

}  // namespace note_inbox
