#include "steps.h"
#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <math.h>
#include <time.h>

namespace steps {

static Preferences s_prefs;
static int      s_today      = 0;
static char     s_today_key[16]  = {0};   // "YYYYMMDD"

// peak-detection state
static float    s_lpf        = 1.0f;     // low-pass filtered magnitude (g)
static float    s_base       = 1.0f;     // very slow baseline
static bool     s_peaking    = false;
static uint32_t s_last_step  = 0;
static uint32_t s_persist_at = 0;

static constexpr uint32_t REFRACTORY_MS = 280;   // min ms between steps
static constexpr float    PEAK_TH       = 0.18f; // (g) above baseline
static constexpr float    RESET_TH      = 0.06f; // must fall below to re-arm
static constexpr uint32_t PERSIST_EVERY_MS = 30 * 1000;

// PT timezone — Stanley's wall clock.
static constexpr const char* TZ_SON = "PST8PDT,M3.2.0,M11.1.0";

static void todayKey(char out[16]) {
    setenv("TZ", TZ_SON, 1); tzset();
    time_t now = time(nullptr);
    struct tm t; localtime_r(&now, &t);
    snprintf(out, 16, "%04d%02d%02d",
             1900 + t.tm_year, t.tm_mon + 1, t.tm_mday);
}

void init() {
    todayKey(s_today_key);
    s_prefs.begin("papa-watch", false);
    char stored_key[16] = {0};
    String s = s_prefs.getString("steps_day", "");
    strncpy(stored_key, s.c_str(), sizeof(stored_key) - 1);
    if (strcmp(stored_key, s_today_key) == 0) {
        s_today = s_prefs.getInt("steps_n", 0);
    } else {
        s_today = 0;
        s_prefs.putString("steps_day", s_today_key);
        s_prefs.putInt("steps_n", 0);
    }
    s_prefs.end();
    Serial.printf("[steps] init day=%s count=%d\n", s_today_key, s_today);
}

int today() { return s_today; }

void resetIfNewDay() {
    char k[16];
    todayKey(k);
    if (strcmp(k, s_today_key) != 0) {
        Serial.printf("[steps] day rollover %s → %s, was %d\n",
                      s_today_key, k, s_today);
        strncpy(s_today_key, k, sizeof(s_today_key));
        s_today = 0;
        s_prefs.begin("papa-watch", false);
        s_prefs.putString("steps_day", s_today_key);
        s_prefs.putInt("steps_n", 0);
        s_prefs.end();
    }
}

void update() {
    if (!M5.Imu.isEnabled()) return;
    if (!M5.Imu.update()) return;

    float ax, ay, az;
    M5.Imu.getAccel(&ax, &ay, &az);
    float mag = sqrtf(ax*ax + ay*ay + az*az);

    // dual-rate filter: lpf (fast) tracks steps, base (slow) tracks gravity drift
    s_lpf  = s_lpf  * 0.85f + mag * 0.15f;
    s_base = s_base * 0.995f + s_lpf * 0.005f;
    float delta = s_lpf - s_base;

    uint32_t now = millis();
    if (!s_peaking && delta > PEAK_TH && (now - s_last_step) > REFRACTORY_MS) {
        s_peaking = true;
        s_last_step = now;
        s_today++;
    } else if (s_peaking && delta < RESET_TH) {
        s_peaking = false;
    }

    // throttled NVS persist
    if (now - s_persist_at > PERSIST_EVERY_MS) {
        s_persist_at = now;
        s_prefs.begin("papa-watch", false);
        s_prefs.putInt("steps_n", s_today);
        s_prefs.end();
    }
}

}  // namespace steps
