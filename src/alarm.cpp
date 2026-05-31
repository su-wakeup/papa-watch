#include "alarm.h"
#include "assets/alarm_chime.h"
#include "tz_helper.h"
#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <time.h>

namespace alarms {

static Config   s_cfg              = { 7, 30, false, REPEAT_DAILY };
static bool     s_fired_this_minute= false;
static bool     s_firing           = false;
static uint32_t s_fire_started_ms  = 0;
static int      s_rings_played     = 0;
static uint32_t s_last_ring_ms     = 0;

// Chime is 4s; play it twice with a small gap → ~9s of total ringing,
// which is long enough to wake without feeling angry.
static constexpr int      RING_COUNT     = 2;
static constexpr uint32_t RING_GAP_MS    = 4500;
static constexpr uint32_t FIRE_MAX_MS    = 30 * 1000;
// Stanley's local timezone is what the alarm time refers to.
static constexpr const char* TZ_SON = "PST8PDT,M3.2.0,M11.1.0";

void load() {
    Preferences p;
    p.begin("papa-watch", true);
    s_cfg.hour    = p.getUChar("alarm_h",  7);
    s_cfg.minute  = p.getUChar("alarm_m", 30);
    s_cfg.enabled = p.getBool ("alarm_on", false);
    s_cfg.mode    = (RepeatMode)p.getUChar("alarm_md", REPEAT_DAILY);
    p.end();
    Serial.printf("[alarm] loaded %02d:%02d en=%d\n",
                  s_cfg.hour, s_cfg.minute, (int)s_cfg.enabled);
}

void save() {
    Preferences p;
    p.begin("papa-watch", false);
    p.putUChar("alarm_h",  s_cfg.hour);
    p.putUChar("alarm_m",  s_cfg.minute);
    p.putBool ("alarm_on", s_cfg.enabled);
    p.putUChar("alarm_md", (uint8_t)s_cfg.mode);
    p.end();
}

Config& get() { return s_cfg; }

void fire() {
    s_firing          = true;
    s_fire_started_ms = millis();
    s_rings_played    = 0;
    s_last_ring_ms    = 0;
    Serial.printf("[alarm] FIRED at scheduled %02d:%02d\n",
                  s_cfg.hour, s_cfg.minute);
}

void dismiss() {
    if (s_firing) {
        Serial.println("[alarm] dismissed by user");
        M5.Speaker.stop();
        s_firing = false;
    }
}

bool isFiring() { return s_firing; }

void check() {
    // Service the active fire: play up to RING_COUNT coin chimes spaced
    // RING_GAP_MS apart, then auto-dismiss after FIRE_MAX_MS or rings done.
    if (s_firing) {
        uint32_t now = millis();
        if (s_rings_played < RING_COUNT
            && (s_last_ring_ms == 0 || now - s_last_ring_ms >= RING_GAP_MS)) {
            M5.Speaker.setVolume(220);
            M5.Speaker.playRaw(alarm_chime_data,
                               alarm_chime_samples,
                               alarm_chime_sample_rate);
            s_last_ring_ms = now;
            s_rings_played++;
        }
        if (s_rings_played >= RING_COUNT
            && now - s_last_ring_ms > 2000) {
            s_firing = false;
        }
        if (now - s_fire_started_ms > FIRE_MAX_MS) {
            s_firing = false;
            M5.Speaker.stop();
        }
        return;
    }

    if (!s_cfg.enabled) {
        s_fired_this_minute = false;
        return;
    }

    // PT-local current time
    time_t now_s = time(nullptr);
    struct tm t; tz_helper::localtime_in(TZ_SON, now_s, &t);

    bool match = (t.tm_hour == s_cfg.hour && t.tm_min == s_cfg.minute);
    if (match && !s_fired_this_minute) {
        s_fired_this_minute = true;
        fire();
        // ONCE mode auto-disarms after the alarm fires; user has to flip the
        // switch back on (or change the time) to set a new wake-up.
        if (s_cfg.mode == REPEAT_ONCE) {
            s_cfg.enabled = false;
            save();
            Serial.println("[alarm] ONCE mode → auto-disabled");
        }
    } else if (!match) {
        s_fired_this_minute = false;
    }
}

}  // namespace alarmss
