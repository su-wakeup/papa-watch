#include "voice_play.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <M5Unified.h>
#include <esp_heap_caps.h>

namespace voice_play {

static char     s_url[256]  = {0};
static bool     s_pending   = false;
static uint8_t* s_buf       = nullptr;   // PSRAM PCM buffer, kept alive while playing

void request(const char* url) {
    if (!url || !url[0]) return;
    strncpy(s_url, url, sizeof(s_url) - 1);
    s_url[sizeof(s_url) - 1] = 0;
    s_pending = true;
}

bool busy() { return s_pending || (s_buf && M5.Speaker.isPlaying()); }

void tick() {
    // reclaim the buffer once playback finishes
    if (s_buf && !M5.Speaker.isPlaying()) { heap_caps_free(s_buf); s_buf = nullptr; }
    if (!s_pending) return;
    if (WiFi.status() != WL_CONNECTED) return;       // retry next tick when the link is up
    s_pending = false;

    HTTPClient http;
    if (!http.begin(s_url)) { Serial.println("[voice] http.begin failed"); return; }
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[voice] GET %d\n", code);
        http.end();
        return;
    }
    int len = http.getSize();                        // expect raw int16 PCM bytes
    if (len <= 0 || len > 2 * 1024 * 1024) {         // sanity cap ~64s @16k
        Serial.printf("[voice] bad length %d\n", len);
        http.end();
        return;
    }
    uint8_t* buf = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    if (!buf) { Serial.println("[voice] PSRAM alloc failed"); http.end(); return; }

    WiFiClient* st = http.getStreamPtr();
    int got = 0;
    uint32_t t0 = millis();
    while (got < len && http.connected() && millis() - t0 < 15000) {
        int avail = st->available();
        if (avail > 0) got += st->readBytes(buf + got, min(avail, len - got));
        else delay(2);
    }
    http.end();
    Serial.printf("[voice] downloaded %d/%d bytes\n", got, len);

    if (got < 2) { heap_caps_free(buf); return; }
    if (s_buf) heap_caps_free(s_buf);
    s_buf = buf;
    M5.Speaker.setVolume(200);
    M5.Speaker.playRaw((const int16_t*)s_buf, got / 2, 16000);
}

}  // namespace voice_play
