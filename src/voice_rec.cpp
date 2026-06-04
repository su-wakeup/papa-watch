#include "voice_rec.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <M5Unified.h>
#include <esp_heap_caps.h>

namespace voice_rec {

static constexpr int      RATE     = 16000;
static constexpr uint32_t REC_MS   = 5000;
static constexpr size_t   CAP      = (size_t)RATE * 5;          // samples (5 s)
// TEMP bench target — the real one becomes the Worker upload route.
static const char*        UPLOAD_URL = "http://192.168.7.166:8000/upload";

enum St { IDLE, REC, UPLOADING };
static St        s_st  = IDLE;
static int16_t*  s_buf = nullptr;
static uint32_t  s_t0  = 0;
static char      s_status[32] = "";

static void restoreSpeaker() {
    M5.Mic.end();
    M5.Speaker.begin();
    auto c = M5.Speaker.config(); c.magnification = 16; M5.Speaker.config(c);
    M5.Speaker.setVolume(180);
}

void start() {
    if (s_st != IDLE) return;
    s_buf = (int16_t*)heap_caps_malloc(CAP * 2, MALLOC_CAP_SPIRAM);
    if (!s_buf) { snprintf(s_status, sizeof(s_status), "no mem"); return; }
    M5.Speaker.end();
    if (!M5.Mic.begin()) {
        heap_caps_free(s_buf); s_buf = nullptr;
        M5.Speaker.begin();
        snprintf(s_status, sizeof(s_status), "mic fail");
        return;
    }
    M5.Mic.record(s_buf, CAP, RATE);          // fills the whole buffer over REC_MS
    s_t0 = millis();
    s_st = REC;
}

void tick() {
    if (s_st != REC) return;
    uint32_t el = millis() - s_t0;
    float rem = el < REC_MS ? (REC_MS - el) / 1000.0f : 0.0f;
    snprintf(s_status, sizeof(s_status), "REC %.1fs", rem);
    if (el < REC_MS && M5.Mic.isRecording()) return;

    restoreSpeaker();
    s_st = UPLOADING;
    snprintf(s_status, sizeof(s_status), "Uploading...");

    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        if (http.begin(UPLOAD_URL)) {
            http.addHeader("Content-Type", "application/octet-stream");
            int code = http.POST((uint8_t*)s_buf, CAP * 2);
            if (code == 200) snprintf(s_status, sizeof(s_status), "Sent OK");
            else             snprintf(s_status, sizeof(s_status), "Send fail %d", code);
            http.end();
        } else {
            snprintf(s_status, sizeof(s_status), "begin fail");
        }
    } else {
        snprintf(s_status, sizeof(s_status), "no wifi");
    }
    heap_caps_free(s_buf); s_buf = nullptr;
    s_st = IDLE;
}

bool        active()     { return s_st != IDLE; }
const char* statusText() { return s_status; }

}  // namespace voice_rec
