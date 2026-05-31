// Father-Son Dual-Timezone Watch — V1
// Layout (round 468x468):
//   ┌─────────────────────────┐
//   │     Wed · May 28        │  ← son's local date, top arc, small
//   │                         │
//   │       21:47             │  ← son's local time, hero, ~72px
//   │                         │
//   │   ●  09:47  Beijing     │  ← dad's time, small, with status dot
//   │      爸爸正在工作          │  ← dad's status, CN, small
//   └─────────────────────────┘

#include <M5Unified.h>
#include <WiFi.h>
#include <time.h>
#include <sys/time.h>
#include "wifi_setup.h"
#include "ntp_sync.h"
#include "lvgl_port.h"
#include "face_boot.h"
#include "face_lcd.h"
#include "face_manager.h"
#include "face_term.h"
#include "face_mech.h"
#include "heart_overlay.h"
#include "status_bar.h"
#include "steps.h"
#include "alarm.h"
#include "geo.h"
#include "wake_gesture.h"
#include "app.h"
#include "app_launcher.h"
#include "app_settings.h"
#include "dad_status.h"
#include "heart_relay.h"
#include "ota.h"
#include <ArduinoJson.h>
#include "audio/cow_moo.h"
#include "assets/boot_jingle.h"

// OTA endpoint — real GitHub Releases now. Pushing a v*.*.* tag to the repo
// triggers .github/workflows/release.yml, which uploads firmware.bin as an
// asset. The on-watch fetcher hits the API below and pulls the newest tag.
// extern-linkable so app_settings can reuse it for the "Check Updates" row.
const char* OTA_MANIFEST_URL = "https://api.github.com/repos/su-wakeup/papa-watch/releases/latest";

// ── geometry ─────────────────────────────────────────────
static constexpr int CENTER_X = 234;
static constexpr int CENTER_Y = 234;
static constexpr int SCREEN_R = 232;

// ── timezones (POSIX TZ strings → automatic DST) ─────────
static constexpr const char* TZ_SON = "PST8PDT,M3.2.0,M11.1.0";  // America/Los_Angeles
static constexpr const char* TZ_DAD = "CST-8";                    // Asia/Shanghai

// ── colors ───────────────────────────────────────────────
static constexpr uint16_t COL_BG       = TFT_BLACK;
static constexpr uint16_t COL_TIME     = 0xFFFF;          // pure white
static constexpr uint16_t COL_DATE     = 0xC618;          // light grey
static constexpr uint16_t COL_DAD      = 0x8C71;          // dim grey-white
static constexpr uint16_t COL_HEART    = 0xF800;          // red
static constexpr uint16_t COL_STAT_AWK = 0x07E0;          // green
static constexpr uint16_t COL_STAT_WRK = 0xFD20;          // amber
static constexpr uint16_t COL_STAT_SLP = 0x4ADF;          // soft blue

// ── status enum ──────────────────────────────────────────
enum DadStatus { DAD_AWAKE, DAD_WORKING, DAD_SLEEPING, DAD_HEART };

// ── interaction tuning ───────────────────────────────────
static constexpr uint32_t HOLD_ARM_MS        = 700;       // long-press threshold
static constexpr int      HOLD_MAX_DRIFT_PX2 = 80 * 80;   // cancel if finger drifts > 80 px AFTER armed
static constexpr uint32_t RECV_TOTAL_MS      = 3420;      // matches drawBloomHeart timeline
static constexpr uint32_t SEND_TOTAL_MS      = 1250;      // matches drawSendHeart timeline

// ── state ────────────────────────────────────────────────
static int      s_last_tick_sec = -1;
static uint32_t s_recv_heart_ms = 0;        // last received from dad
static M5Canvas s_face(nullptr);            // off-screen buffer

// touch-hold-to-send
static uint32_t s_hold_start_ms = 0;
static bool     s_hold_armed    = false;
static int      s_hold_start_x  = 0;
static int      s_hold_start_y  = 0;

// outgoing send animation
static uint32_t s_send_heart_ms = 0;
static int      s_send_x        = 234;
static int      s_send_y        = 234;

// debug status override (BtnB cycles)
static int      s_dbg_status_cycle = 0;

// unread message counter: bumped on every received heart, cleared by short-tap
static int      s_unread_count = 0;

// ── heart geometry ───────────────────────────────────────
// Draws a real heart shape (two filled circles + smooth triangle).
// `height` = total visual height; heart centered on (cx, cy).
static void drawHeart(M5Canvas& g, int cx, int cy, int height, uint16_t color) {
    if (height < 4) return;
    float r          = height * 0.34f;
    int   lobe_off_x = (int)(r * 0.78f);
    int   lobe_off_y = -(int)(height * 0.16f);
    int   rint       = (int)r;
    int   tip_y      = cy + (int)(height * 0.52f);
    int   tri_y      = cy + lobe_off_y + (int)(r * 0.30f);  // 17° below lobe center
    int   tri_left   = cx - lobe_off_x - (int)(r * 0.96f);
    int   tri_right  = cx + lobe_off_x + (int)(r * 0.96f);

    g.fillCircle(cx - lobe_off_x, cy + lobe_off_y, rint, color);
    g.fillCircle(cx + lobe_off_x, cy + lobe_off_y, rint, color);
    g.fillTriangle(tri_left, tri_y, tri_right, tri_y, cx, tip_y, color);
}

// fade a RGB565 red toward black by reducing red channel only
static uint16_t fadeRed(float p) {  // p: 0=full red, 1=black
    if (p < 0) p = 0; if (p > 1) p = 1;
    int r = (int)(31 * (1.0f - p));
    return (uint16_t)((r & 0x1F) << 11);
}

// Animate a heart blooming from 0 to max size, then dimming.
// `elapsed_ms` = ms since the bloom started. Returns true while still drawing.
static bool drawBloomHeart(M5Canvas& g, int cx, int cy, uint32_t elapsed_ms) {
    constexpr uint32_t GROW_MS  = 320;
    constexpr uint32_t HOLD_MS  = 1600;
    constexpr uint32_t FADE_MS  = 1500;
    constexpr int      MAX_SIZE = 220;

    if (elapsed_ms >= GROW_MS + HOLD_MS + FADE_MS) return false;

    int      size;
    uint16_t color;
    if (elapsed_ms < GROW_MS) {
        // ease-out cubic from 0 to MAX_SIZE
        float t = elapsed_ms / (float)GROW_MS;
        float eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
        size  = (int)(MAX_SIZE * eased);
        color = fadeRed(0.0f);
    } else if (elapsed_ms < GROW_MS + HOLD_MS) {
        size  = MAX_SIZE;
        color = fadeRed(0.0f);
    } else {
        float t = (elapsed_ms - GROW_MS - HOLD_MS) / (float)FADE_MS;
        size  = MAX_SIZE;
        color = fadeRed(t);
    }
    drawHeart(g, cx, cy, size, color);
    return true;
}

// Send animation: heart at finger pos → grows fast → drifts up + fades.
// Visually distinct from receive (pink-fading, upward drift, single cycle).
static bool drawSendHeart(M5Canvas& g, int cx, int cy, uint32_t elapsed_ms) {
    constexpr uint32_t GROW_MS = 350;
    constexpr uint32_t FADE_MS = 900;
    constexpr int      MAX_SZ  = 280;
    if (elapsed_ms >= GROW_MS + FADE_MS) return false;

    int      size;
    int      y_off = 0;
    uint16_t color;
    if (elapsed_ms < GROW_MS) {
        float p = elapsed_ms / (float)GROW_MS;
        float eased = 1.0f - (1.0f - p) * (1.0f - p);
        size  = (int)(MAX_SZ * eased);
        color = 0xFC1F;                                  // pink-magenta
    } else {
        float p = (elapsed_ms - GROW_MS) / (float)FADE_MS;
        size  = MAX_SZ;
        y_off = -(int)(p * p * 90);                      // accelerate upward
        int r = (int)(31 * (1.0f - p));
        int b = (int)(31 * (1.0f - p));
        color = (uint16_t)((r << 11) | b);
    }
    drawHeart(g, cx, cy + y_off, size, color);
    return true;
}

// ── haptics ──────────────────────────────────────────────
static void hapticClick() {     // "you've held long enough — armed"
    M5.Power.setVibration(180); delay(40); M5.Power.setVibration(0);
}
static void hapticPulse() {     // "fired — heart is sent"
    M5.Power.setVibration(240); delay(160); M5.Power.setVibration(0);
}
static void hapticHeartbeat() { // "received — dad sent you one" (lub-dub)
    M5.Power.setVibration(200); delay(90);  M5.Power.setVibration(0); delay(110);
    M5.Power.setVibration(200); delay(90);  M5.Power.setVibration(0);
}

// Stanley's dad is Year-of-the-Ox. A real cow moo on heart-received.
// Sample: Wikimedia Commons "Mudchute_cow_1.ogg" (mono, 2.19 s, decoded to
// 16 kHz int16 PCM and baked into firmware as src/audio/cow_moo.h).
static void chimeReceive() {
    if (!M5.Speaker.isEnabled()) return;
    M5.Speaker.setVolume(255);
    bool ok = M5.Speaker.playRaw(cow_moo, cow_moo_n, cow_moo_sample_rate);
    Serial.printf("[moo] playRaw real-sample ok=%d  samples=%u  rate=%d\n",
                  (int)ok, (unsigned)cow_moo_n, cow_moo_sample_rate);
}

// Check if a string is pure ASCII (lets us decide whether to display the
// dad-supplied note as-is or fall back to English while we don't have a CJK font).
static bool isPureAscii(const char* s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}

// ── helpers ──────────────────────────────────────────────
static void getLocalAt(const char* tz, struct tm& out) {
    setenv("TZ", tz, 1);
    tzset();
    time_t now = time(nullptr);
    localtime_r(&now, &out);
}

static DadStatus computeDadStatus(const struct tm& dad_tm) {
    int h = dad_tm.tm_hour;
    if (h >= 23 || h < 7)  return DAD_SLEEPING;
    if (h >= 9  && h < 18) return DAD_WORKING;
    return DAD_AWAKE;
}

static uint16_t statusColor(DadStatus s) {
    switch (s) {
        case DAD_AWAKE:    return COL_STAT_AWK;
        case DAD_WORKING:  return COL_STAT_WRK;
        case DAD_SLEEPING: return COL_STAT_SLP;
        case DAD_HEART:    return COL_HEART;
    }
    return COL_DATE;
}

static const char* statusTextCN(DadStatus s) {
    switch (s) {
        case DAD_AWAKE:    return "爸爸醒着";
        case DAD_WORKING:  return "爸爸在工作";
        case DAD_SLEEPING: return "爸爸在睡觉";
        case DAD_HEART:    return "爸爸刚刚想你了";
    }
    return "";
}

static const char* dayNameEN(int wday) {
    static const char* d[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    return d[wday & 7 ? wday : 0];
}
static const char* monthNameEN(int mon) {
    static const char* m[] = {"Jan","Feb","Mar","Apr","May","Jun",
                              "Jul","Aug","Sep","Oct","Nov","Dec"};
    return m[mon % 12];
}

// ── time bootstrap: seed system clock from compile-time if RTC invalid ──
static void seedClockIfNeeded() {
    time_t now = time(nullptr);
    struct tm utc;
    gmtime_r(&now, &utc);
    if (utc.tm_year + 1900 >= 2024) return;   // already plausible

    // __DATE__ "May 29 2026"  __TIME__ "08:30:00"  → treat as Beijing local
    struct tm c = {};
    char mon[4]; int day, year, hh, mm, ss;
    sscanf(__DATE__, "%3s %d %d", mon, &day, &year);
    sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss);
    static const char* names = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char* p = strstr(names, mon);
    c.tm_mon  = p ? (p - names) / 3 : 0;
    c.tm_mday = day;
    c.tm_year = year - 1900;
    c.tm_hour = hh; c.tm_min = mm; c.tm_sec = ss;

    setenv("TZ", TZ_DAD, 1); tzset();         // interpret as Beijing
    time_t beijing_local_as_utc = mktime(&c); // converts using current TZ → real UTC
    struct timeval tv = { beijing_local_as_utc, 0 };
    settimeofday(&tv, nullptr);

    Serial.printf("[clock] seeded from build: %s %s (Beijing)\n", __DATE__, __TIME__);
}

// ── canvas drawing (all on s_face, then push) ────────────
static void drawFace(DadStatus dad_st) {
    struct tm son, dad;
    getLocalAt(TZ_SON, son);
    getLocalAt(TZ_DAD, dad);

    s_face.fillScreen(COL_BG);

    // outer thin ring
    s_face.drawCircle(CENTER_X, CENTER_Y, SCREEN_R, 0x2104);
    s_face.drawCircle(CENTER_X, CENTER_Y, SCREEN_R - 1, 0x2104);

    // top: son's date — "Wed · May 28"
    char date_buf[32];
    snprintf(date_buf, sizeof(date_buf), "%s . %s %d",
             dayNameEN(son.tm_wday), monthNameEN(son.tm_mon), son.tm_mday);
    s_face.setTextDatum(top_center);
    s_face.setTextColor(COL_DATE, COL_BG);
    s_face.setFont(&fonts::FreeSans12pt7b);
    s_face.drawString(date_buf, CENTER_X, 70);

    // hero: son's time — "21:47" giant, modern sci-fi font
    char time_buf[8];
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d", son.tm_hour, son.tm_min);
    s_face.setTextColor(COL_TIME, COL_BG);
    s_face.setFont(&fonts::DejaVu72);
    s_face.setTextDatum(middle_center);
    s_face.drawString(time_buf, CENTER_X, CENTER_Y - 10);

    // dad row: ● 09:47 Beijing
    int dad_row_y = CENTER_Y + 90;
    int dot_r = 7;
    int dot_x = CENTER_X - 90;
    s_face.fillCircle(dot_x, dad_row_y, dot_r, statusColor(dad_st));
    s_face.drawCircle(dot_x, dad_row_y, dot_r + 2, statusColor(dad_st));

    char dad_buf[24];
    snprintf(dad_buf, sizeof(dad_buf), "%02d:%02d  Beijing", dad.tm_hour, dad.tm_min);
    s_face.setTextDatum(middle_left);
    s_face.setTextColor(COL_DAD, COL_BG);
    s_face.setFont(&fonts::FreeSans12pt7b);
    s_face.drawString(dad_buf, dot_x + 20, dad_row_y);

    // dad status (Chinese), below
    s_face.setTextDatum(top_center);
    s_face.setTextColor(statusColor(dad_st), COL_BG);
    s_face.setFont(&fonts::efontCN_16);
    s_face.drawString(statusTextCN(dad_st), CENTER_X, dad_row_y + 24);

}

// ── frame composition: face + overlays + push ────────────
static void renderFrame(bool finger_down, int /*touch_x*/, int /*touch_y*/) {
    struct tm dad; getLocalAt(TZ_DAD, dad);
    DadStatus st = computeDadStatus(dad);

    bool recv_active = s_recv_heart_ms && (millis() - s_recv_heart_ms < RECV_TOTAL_MS);
    bool send_active = s_send_heart_ms && (millis() - s_send_heart_ms < SEND_TOTAL_MS);
    bool holding     = s_hold_start_ms && finger_down;

    if (recv_active)              st = DAD_HEART;
    else if (s_dbg_status_cycle)  st = (DadStatus)(s_dbg_status_cycle - 1);

    drawFace(st);

    if (send_active) {
        drawSendHeart(s_face, s_send_x, s_send_y, millis() - s_send_heart_ms);
    }
    if (recv_active) {
        drawBloomHeart(s_face, CENTER_X, CENTER_Y - 10, millis() - s_recv_heart_ms);
    }
    if (holding) {
        uint32_t held = millis() - s_hold_start_ms;
        if (s_hold_armed) {
            int pulse_sz = 86 + (int)(8 * sinf(millis() / 110.0f));
            drawHeart(s_face, s_hold_start_x, s_hold_start_y, pulse_sz, COL_HEART);
        } else if (held > 80) {
            float p = (held > HOLD_ARM_MS) ? 1.0f : held / (float)HOLD_ARM_MS;
            int   size = 22 + (int)(58 * p);
            int   r    = 16 + (int)(15 * p);
            drawHeart(s_face, s_hold_start_x, s_hold_start_y, size,
                      (uint16_t)((r & 0x1F) << 11));
        }
    }

    s_face.pushSprite(&M5.Display, 0, 0);
}

// ── boot banner (3s) ─────────────────────────────────────
static void drawBootBanner() {
    auto& d = M5.Display;
    d.fillScreen(COL_BG);
    d.drawCircle(CENTER_X, CENTER_Y, SCREEN_R, 0x2104);

    d.setTextDatum(middle_center);
    d.setTextColor(COL_TIME, COL_BG);
    d.setFont(&fonts::Orbitron_Light_32);
    d.drawString("STANLEY", CENTER_X, CENTER_Y - 50);

    // real heart, not <3
    s_face.fillScreen(COL_BG);                          // reuse canvas as scratch
    drawHeart(s_face, CENTER_X, CENTER_Y + 30, 100, COL_HEART);
    s_face.pushSprite(&M5.Display, 0, 0);
    // re-draw text after the canvas push covered it
    d.setTextColor(COL_TIME, COL_BG);
    d.setFont(&fonts::Orbitron_Light_32);
    d.drawString("STANLEY", CENTER_X, CENTER_Y - 50);

    d.setTextColor(COL_DATE);
    d.setFont(&fonts::efontCN_16);
    d.drawString("一直在你身边", CENTER_X, CENTER_Y + 100);
}

void setup() {
    auto cfg = M5.config();
    cfg.clear_display = true;
    cfg.output_power  = true;
    cfg.internal_imu  = true;
    cfg.internal_rtc  = true;
    cfg.internal_spk  = true;
    M5.begin(cfg);

    Serial.begin(115200);
    delay(800);

    M5.Display.setRotation(0);
    M5.Display.setBrightness(200);

    // M5Unified's default speaker config for the StopWatch sets magnification=1,
    // which makes everything inaudibly quiet through the tiny 1318 cavity speaker.
    // Crank it up before any playback.
    {
        auto spkCfg = M5.Speaker.config();
        spkCfg.magnification = 16;     // 24 distorted the 1318 cavity speaker
        M5.Speaker.config(spkCfg);
        M5.Speaker.begin();
    }

    // off-screen canvas for flicker-free redraws
    s_face.setPsram(true);
    s_face.setColorDepth(16);
    s_face.createSprite(468, 468);

    // time bootstrap order: RTC chip (battery) → compile-time fallback
    if (!ntp_sync::loadFromRtc()) {
        seedClockIfNeeded();
    }

    // (the old M5GFX boot banner has been replaced by face_boot's LVGL animation,
    //  which fires once LVGL is up after WiFi+NTP — see below)

    Serial.println();
    Serial.println("================ BOOT ================");
    Serial.printf("[boot] WiFi: trying saved credentials...\n");

    // Hold both buttons during boot to wipe saved Wi-Fi credentials
    M5.update();
    if (M5.BtnA.isPressed() && M5.BtnB.isPressed()) {
        wifi_setup::forgetSaved();
        Serial.println("[boot] WiFi credentials wiped by user");
    }

    if (!wifi_setup::tryAutoConnect(8000)) {
        Serial.println("[boot] auto-connect failed → interactive config");
        wifi_setup::runInteractiveConfig();
    }
    Serial.printf("[boot] WiFi connected: %s  IP=%s\n",
                  wifi_setup::currentSSID().c_str(),
                  WiFi.localIP().toString().c_str());

    // Pull real time from NTP and persist to hardware RTC.
    ntp_sync::syncTime(8000);

    // LVGL takes over the panel from here. Boot banner + wifi flow were direct M5GFX;
    // the watch face is now driven by LVGL widgets.
    lvgl_port::begin(468, 468);
    // "Hello Stanley" greeting animation, then the LCD face. The transition
    // happens in loop() once face_boot::finished() returns true.
    face_boot::create();

    // ── heartbeat relay over MQTT ──
    heart_relay::setOnReceive([](const char* payload, unsigned int len) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload, len);
        if (err) {
            Serial.printf("[heart] JSON parse error: %s\n", err.c_str());
            hapticHeartbeat();
            chimeReceive();
            face_lcd::showAlert("DAD HEART", 0x90FFA6, 4000);
            return;
        }

        // OTA command: {"cmd":"ota"}
        const char* cmd = doc["cmd"] | (const char*)nullptr;
        if (cmd && strcmp(cmd, "ota") == 0) {
            Serial.println("[mqtt] OTA command received");
            face_lcd::showAlert("UPDATING...", 0xFD20, 8000);
            ota::checkAndUpdate(OTA_MANIFEST_URL, true);
            return;
        }

        // Status override: {"cmd":"status","value":"free|busy|away|thinking|auto"}
        if (cmd && strcmp(cmd, "status") == 0) {
            const char* val = doc["value"] | "auto";
            dad_status::setOverride(val, 6);   // 6-hour window
            char alert[24] = "DAD ";
            for (size_t i = 0; val[i] && i < 18; i++) {
                char c = val[i];
                alert[i + 4] = (c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c;
                alert[i + 5] = 0;
            }
            face_lcd::showAlert(alert,
                dad_status::color(dad_status::current(0)),
                3000);
            return;
        }

        // Heart payload: {"t":..,"from":"..","note":".."}
        const char* note = doc["note"]  | "DAD HEART";
        const char* from = doc["from"]  | "";

        Serial.printf("[heart] from=%s note=%s\n", from, note);

        hapticHeartbeat();
        chimeReceive();
        s_unread_count++;
        heart_overlay::trigger();        // big red heart blooms over whatever face is showing

        // Keep the LCD bezel text-alert too for the note content (when on face_lcd).
        char alert_buf[40];
        size_t note_len = strlen(note);
        if (note_len > 0 && note_len < sizeof(alert_buf) - 1 && isPureAscii(note, note_len)) {
            for (size_t i = 0; i < note_len; i++) {
                char c = note[i];
                alert_buf[i] = (c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c;
            }
            alert_buf[note_len] = 0;
            face_lcd::showAlert(alert_buf, 0x90FFA6, 5000);
        } else {
            face_lcd::showAlert("DAD HEART", 0x90FFA6, 4000);
        }
    });
    heart_relay::begin("stanley-dad-2026");

    Serial.printf("[ota] firmware v%s\n", ota::FIRMWARE_VERSION);

    // ── boot beep: prove speaker output works ──
    Serial.printf("[spk] enabled=%d  vol=%d\n",
                  M5.Speaker.isEnabled(), M5.Speaker.getVolume());
    // Boot jingle: pre-synthesized "coins falling" PCM (FM-bell synthesis,
    // 1.7s @ 16kHz int16 mono, ~53KB in flash). Square-wave tone() can't make
    // a real metallic timbre because it has no inharmonic partials — coins
    // need the FM synthesis baked into the asset.
    M5.Speaker.setVolume(180);
    M5.Speaker.playRaw(boot_jingle_data, boot_jingle_samples,
                       boot_jingle_sample_rate);
    delay(boot_jingle_samples * 1000UL / boot_jingle_sample_rate + 50);
    Serial.printf("[spk] coin jingle dispatched (%u samples @ %u Hz)\n",
                  (unsigned)boot_jingle_samples, boot_jingle_sample_rate);

    Serial.printf("[boot] display %dx%d  PSRAM=%uKB  free=%uKB\n",
                  M5.Display.width(), M5.Display.height(),
                  (unsigned)(ESP.getPsramSize()/1024),
                  (unsigned)(ESP.getFreeHeap()/1024));
    Serial.printf("[boot] IMU=%d RTC=%d TOUCH=%d SPK=%d\n",
                  M5.Imu.isEnabled(), M5.Rtc.isEnabled(),
                  M5.Touch.isEnabled(), M5.Speaker.isEnabled());

    {
        // Probe magnetometer over a second: get 10 samples spaced ~100ms.
        // If they're all 0.0 the BMM150 isn't really there (M5Unified
        // happily 'detects' a chip whose I2C address ACKs even when nothing
        // is wired behind it). M5StopWatch's official sensor list only
        // includes BMI270 → no magnetic compass possible without mod.
        float min_mag = 1e9f, max_mag = -1e9f;
        for (int i = 0; i < 10; i++) {
            M5.Imu.update();
            float mx = 0, my = 0, mz = 0;
            M5.Imu.getMag(&mx, &my, &mz);
            float m = sqrtf(mx*mx + my*my + mz*mz);
            if (m < min_mag) min_mag = m;
            if (m > max_mag) max_mag = m;
            Serial.printf("[mag] s%d: (%.1f, %.1f, %.1f) |M|=%.1f uT\n",
                          i, mx, my, mz, m);
            delay(100);
        }
        Serial.printf("[mag] verdict: range %.1f..%.1f uT %s\n",
                      min_mag, max_mag,
                      (max_mag < 0.1f) ? "→ NO magnetometer chip" : "→ chip alive");
    }

    steps::init();
    alarms::load();
    wake_gesture::init();
    // Prime the geo cache once at boot, when we've just confirmed WiFi.
    // Avoids the ~3s sync HTTP call running inside app_sundial::enter()
    // (which was blocking LVGL → mech-face second hand jumping forward).
    {
        geo::Location loc;
        if (geo::getLocation(&loc)) {
            Serial.printf("[geo] %s  %.3f, %.3f\n", loc.city, loc.lat, loc.lon);
        } else {
            Serial.println("[geo] prime failed; sundial will try again later");
        }
    }
    app_settings::apply_on_boot();    // brightness / volume / vibrate from NVS

    Serial.println("=======================================");
}

void loop() {
    // ── frame-budget watchdog ──
    // If any single loop iteration exceeds 150ms, log it with which sections
    // ran. This is what tells us next time whether the freeze is MQTT,
    // LVGL render, audio, or something else — so we don't have to guess.
    static uint32_t s_loop_t0 = 0;
    uint32_t loop_dt = s_loop_t0 ? millis() - s_loop_t0 : 0;
    if (loop_dt > 150) {
        Serial.printf("[loop] LONG iter %ums (heap free=%uKB)\n",
                      (unsigned)loop_dt,
                      (unsigned)(ESP.getFreeHeap() / 1024));
    }
    s_loop_t0 = millis();

    M5.update();

    // ── LVGL drives the panel; we just feed the tick and refresh the face once/sec ──
    uint32_t t1 = millis();
    lvgl_port::tick();
    uint32_t t2 = millis();
    heart_relay::tick();
    uint32_t t3 = millis();
    steps::update();
    uint32_t t4 = millis();
    wake_gesture::update();

    if (loop_dt > 150) {
        Serial.printf("  ↳ lvgl=%ums  mqtt=%ums  steps=%ums\n",
                      (unsigned)(t2 - t1),
                      (unsigned)(t3 - t2),
                      (unsigned)(t4 - t3));
    }

    // ── heap safety net — known runtime leak (≈200B/s) drains 160KB boot
    // heap to zero in 5-6h. Until we find the leak, auto-reboot below 30KB
    // so the watch wakes back to a healthy state and the user only loses
    // a few seconds of UI continuity per cycle. Hearts in flight are
    // QoS-1 cached at the broker, so none are lost.
    {
        static uint32_t s_last_heap_check_ms = 0;
        if (millis() - s_last_heap_check_ms > 10000) {
            s_last_heap_check_ms = millis();
            uint32_t free_kb = ESP.getFreeHeap() / 1024;
            if (free_kb < 30) {
                Serial.printf("[heap] CRITICAL %uKB — auto-restarting\n",
                              (unsigned)free_kb);
                delay(200);
                ESP.restart();
            }
        }
    }

    // Boot greeting first; then land on the launcher (v0.9.0 — was face_manager
    // pre-launcher). The launcher's "Watch" cell still routes to face_manager,
    // so the path to time is two taps from cold boot.
    static bool s_boot_done = false;
    if (!s_boot_done) {
        face_boot::update();
        if (face_boot::finished()) {
            face_boot::destroy();
            app_runtime::switch_to(g_apps[0]);   // [0] = APP_LAUNCHER
            heart_overlay::create();
            s_boot_done = true;
        }
        delay(5);
        return;
    }
    heart_overlay::update();
    app_runtime::tick();

    // ── touch hold → arm → release → send heart to dad ──
    // Only active on the actual watch-face tiles (LCD and Mechanical).
    // In Alarm, Stopwatch, Settings, Sundial etc., a 700ms long-press would
    // otherwise compete with whatever button / switch / roller the finger
    // is on and steal the gesture. The watch face is the primary heart-send
    // surface; sub-tiles for controls stay tap-only.
    auto tt2 = M5.Touch.getDetail();
    bool heart_gesture_ok =
        (app_runtime::current() == g_apps[1])           // WATCH app
        && (face_manager::currentIndex() != 2);         // not Alarm tile

    if (tt2.wasPressed()) {
        wake_gesture::notifyActivity();    // any touch keeps screen alive
        if (heart_gesture_ok) {
            s_hold_start_ms = millis();
            s_hold_start_x  = tt2.x;
            s_hold_start_y  = tt2.y;
            s_hold_armed    = false;
        }
    }
    if (s_hold_start_ms && tt2.isPressed()) {
        // Only enforce drift-cancel AFTER the hold is armed. Before that, a
        // brief tap that wobbles a few dozen pixels must still register on
        // release — otherwise short taps to clear unread badge get eaten.
        if (s_hold_armed) {
            int dx = tt2.x - s_hold_start_x;
            int dy = tt2.y - s_hold_start_y;
            if (dx*dx + dy*dy > HOLD_MAX_DRIFT_PX2) {
                s_hold_start_ms = 0;
                s_hold_armed    = false;
            }
        } else if (millis() - s_hold_start_ms >= HOLD_ARM_MS) {
            s_hold_armed = true;
            hapticClick();
            face_lcd::showAlert("ARMED", 0xFD20, 700);
        }
    }
    if (tt2.wasReleased() && s_hold_start_ms) {
        uint32_t held = millis() - s_hold_start_ms;
        if (held >= HOLD_ARM_MS) {
            bool ok = heart_relay::sendHeart();
            hapticPulse();
            face_lcd::showAlert(ok ? "HEART SENT" : "NO NET", 0x42FF7E, 3000);
            Serial.printf("[heart] sent: %s held=%ums\n", ok ? "OK" : "FAIL", held);
        } else if (s_unread_count > 0) {
            // Short tap on watch face → mark all hearts as seen
            Serial.printf("[unread] cleared %d (tap %ums)\n", s_unread_count, held);
            s_unread_count = 0;
            face_lcd::showAlert("READ", 0x42FF7E, 1200);
            face_lcd::setStatus(WiFi.status() == WL_CONNECTED,
                                heart_relay::isConnected(),
                                s_unread_count);
        } else {
            Serial.printf("[tap] %ums no-op (no unread)\n", held);
        }
        s_hold_start_ms = 0;
        s_hold_armed    = false;
    }

    // ── A / B / A+B buttons ────────────────────────────────────────────
    //   on launcher:  A solo → rotate left,  B solo → rotate right
    //   in any app:   A+B simultaneous → back to launcher
    //
    // Combo detection: as soon as both buttons are seen held simultaneously
    // we latch s_combo_fired and run the back-action exactly once. Individual
    // wasReleased() events during the same hold get suppressed so the user
    // doesn't get a stray rotation when they let A go a few ms before B.
    {
        static bool s_combo_fired = false;
        bool a_held = M5.BtnA.isPressed();
        bool b_held = M5.BtnB.isPressed();

        if (a_held || b_held) wake_gesture::notifyActivity();
        if (a_held && b_held && !s_combo_fired) {
            s_combo_fired = true;
            if (app_runtime::current() != g_apps[0]) {
                app_runtime::back_to_launcher();
            }
        }

        // Solo releases dispatch through the current app's optional A/B
        // callbacks (set in app_registry.cpp). Launcher uses these for
        // wheel rotate; Stopwatch uses them for run/lap.
        if (M5.BtnA.wasReleased() && !s_combo_fired) {
            const App* cur = app_runtime::current();
            if (cur && cur->on_button_a) cur->on_button_a();
        }
        if (M5.BtnB.wasReleased() && !s_combo_fired) {
            const App* cur = app_runtime::current();
            if (cur && cur->on_button_b) cur->on_button_b();
        }
        if (!a_held && !b_held) s_combo_fired = false;
    }

    time_t now = time(nullptr);
    struct tm tt; localtime_r(&now, &tt);
    if (tt.tm_sec != s_last_tick_sec) {
        s_last_tick_sec = tt.tm_sec;
        steps::resetIfNewDay();
        alarms::check();
        // face_manager::update() is now driven by app_runtime::tick() above —
        // the Watch app's tick trampoline forwards to it. Status writes below
        // are no-ops on faces whose s_root is null, so they're safe to keep
        // calling regardless of which app is active.
        bool wifi_up = WiFi.status() == WL_CONNECTED;
        bool mqtt_up = heart_relay::isConnected();
        face_lcd::setStatus(wifi_up, mqtt_up, s_unread_count);
        face_mech::setStatus(wifi_up, mqtt_up);
        status_bar::update(wifi_up, mqtt_up, s_unread_count);    // no-op off launcher
        face_term::setUnread(s_unread_count);
        face_mech::setUnread(s_unread_count);
    }

    delay(5);
}
