#include "wake_gesture.h"
#include "app_settings.h"
#include <Arduino.h>
#include <M5Unified.h>
#include <lvgl.h>

namespace wake_gesture {

static constexpr uint32_t SLEEP_AFTER_MS    = 20000;
static constexpr uint32_t SAMPLE_PERIOD_MS  = 100;     // 10Hz IMU check
static constexpr float    WAKE_Z_HIGH       = 0.55f;   // face toward user (raised)
static constexpr float    WAKE_Z_LOW        = 0.40f;   // face sideways/down (arm hanging)
// L1: 80MHz keeps APB at 80MHz, so I2S/UART/touch/WiFi are untouched — the
// lowest CPU step with zero peripheral side effects. WiFi also needs ≥80MHz.
static constexpr uint32_t CPU_AWAKE_MHZ     = 240;
static constexpr uint32_t CPU_SLEEP_MHZ     = 80;

static bool     s_sleeping        = false;
static uint32_t s_last_activity_ms = 0;
static uint32_t s_last_sample_ms   = 0;
static bool     s_saw_low         = false;   // az dipped low since we fell asleep

static void wake() {
    if (!s_sleeping) return;
    s_sleeping = false;
    s_saw_low  = false;                                 // re-arm for next cycle
    setCpuFrequencyMhz(CPU_AWAKE_MHZ);                  // L1: full speed
    M5.Display.wakeup();                                // L3: panel on
    M5.Display.setBrightness(app_settings::brightness());
    lv_obj_invalidate(lv_scr_act());                   // repaint — panel was off
    Serial.println("[wake] up");
}

static void sleep() {
    if (s_sleeping) return;
    s_sleeping = true;
    M5.Display.sleep();                                 // L3: pixels unpowered
    setCpuFrequencyMhz(CPU_SLEEP_MHZ);                  // L1: 80MHz
    Serial.println("[wake] sleep: panel off, cpu 80MHz");
}

void init() {
    s_last_activity_ms = millis();
    s_sleeping = false;
}

void notifyActivity() {
    s_last_activity_ms = millis();
    if (s_sleeping) wake();
}

void update() {
    uint32_t now = millis();
    if (now - s_last_sample_ms < SAMPLE_PERIOD_MS) return;
    s_last_sample_ms = now;

    if (M5.Imu.isEnabled()) {
        M5.Imu.update();
        float ax, ay, az;
        M5.Imu.getAccel(&ax, &ay, &az);

        // Wrist-raise: az dipped LOW (arm hanging, face sideways) and has now
        // risen past HIGH (face toward user). A real raise takes a few 100ms
        // samples to cross the dead band, so we LATCH the low state instead of
        // requiring low→high on adjacent samples — the old way missed every
        // raise that lingered mid-band for more than one sample.
        if (s_sleeping) {
            if (az < WAKE_Z_LOW)               s_saw_low = true;
            if (s_saw_low && az > WAKE_Z_HIGH)  notifyActivity();
        }
    }

    if (!s_sleeping && now - s_last_activity_ms > SLEEP_AFTER_MS) {
        sleep();
    }
}

bool isSleeping() { return s_sleeping; }

}  // namespace wake_gesture
