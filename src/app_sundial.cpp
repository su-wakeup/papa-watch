#include "app_sundial.h"
#include "sun.h"
#include "geo.h"
#include <lvgl.h>
#include <M5Unified.h>
#include <Arduino.h>
#include <time.h>
#include <stdio.h>
#include <math.h>

LV_FONT_DECLARE(mont_light_14);
LV_FONT_DECLARE(mont_light_20);
LV_FONT_DECLARE(mont_light_24);
LV_FONT_DECLARE(mont_light_32);
LV_FONT_DECLARE(mont_light_72_digits);

namespace app_sundial {

static lv_obj_t* s_root          = nullptr;
static lv_obj_t* s_az_lbl        = nullptr;
static lv_obj_t* s_dir_lbl       = nullptr;
static lv_obj_t* s_elev_lbl      = nullptr;
static lv_obj_t* s_hint_lbl      = nullptr;
static lv_obj_t* s_loc_lbl       = nullptr;
static lv_obj_t* s_status_lbl    = nullptr;
// Level lens (bottom half)
static lv_obj_t* s_level_lens    = nullptr;
static lv_obj_t* s_level_bubble  = nullptr;
static lv_obj_t* s_level_text    = nullptr;
static lv_obj_t* s_crosshair_h   = nullptr;
static lv_obj_t* s_crosshair_v   = nullptr;

static uint32_t s_last_imu_ms = 0;

static constexpr int LEVEL_CY     = 130;
static constexpr int LEVEL_R      = 60;
static constexpr int BUBBLE_R     = 16;
static constexpr float DEG_TO_PX  = 2.8f;
static constexpr float LEVEL_TH   = 2.5f;

static geo::Location s_loc       = {0, 0, {0}};
static bool         s_has_loc    = false;
static uint32_t     s_last_tick_sec = 0;

static const char* eight_way(float deg) {
    static const char* labels[16] = {
        "N","NNE","NE","ENE","E","ESE","SE","SSE",
        "S","SSW","SW","WSW","W","WNW","NW","NNW"
    };
    int idx = (int)((deg + 11.25f) / 22.5f) % 16;
    if (idx < 0) idx += 16;
    return labels[idx];
}

// Given the sun's azimuth (true-north reference), if the user points the
// watch's 12-o'clock position at the sun, where on the dial would real North
// appear? Returns a "1..12" o'clock label. North is at azimuth 0°; the sun
// is at azimuth A; from the watch's view (12 = sun), North is at angle
// (-A) clockwise from 12, equivalently (360-A)/30 hour marks.
static int north_clock_position(float sun_az) {
    float h = (360.0f - sun_az) / 30.0f;       // hours, 0..12
    int hr = (int)(h + 0.5f);                  // round to nearest hour
    if (hr <= 0) hr = 12;
    if (hr > 12) hr -= 12;
    return hr;
}

void enter(lv_obj_t* parent) {
    s_root = parent;
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0x100A05), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);

    s_status_lbl = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_status_lbl, &mont_light_14, 0);
    lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0x806848), 0);
    lv_label_set_text(s_status_lbl, "SUN BEARING");
    lv_obj_align(s_status_lbl, LV_ALIGN_CENTER, 0, -210);

    // Hero azimuth — amber number (shrunk to make room for the bubble below).
    s_az_lbl = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_az_lbl, &mont_light_32, 0);
    lv_obj_set_style_text_color(s_az_lbl, lv_color_hex(0xE6A050), 0);
    lv_label_set_text(s_az_lbl, "--°");
    lv_obj_align(s_az_lbl, LV_ALIGN_CENTER, -45, -175);

    s_dir_lbl = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_dir_lbl, &mont_light_32, 0);
    lv_obj_set_style_text_color(s_dir_lbl, lv_color_hex(0xF5E8D0), 0);
    lv_label_set_text(s_dir_lbl, "--");
    lv_obj_align(s_dir_lbl, LV_ALIGN_CENTER, 50, -175);

    s_elev_lbl = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_elev_lbl, &mont_light_14, 0);
    lv_obj_set_style_text_color(s_elev_lbl, lv_color_hex(0xCFC2A8), 0);
    lv_label_set_text(s_elev_lbl, "elevation --°");
    lv_obj_align(s_elev_lbl, LV_ALIGN_CENTER, 0, -140);

    // Multi-line hint
    s_hint_lbl = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_hint_lbl, &mont_light_20, 0);
    lv_obj_set_style_text_color(s_hint_lbl, lv_color_hex(0xF5E8D0), 0);
    lv_obj_set_style_text_align(s_hint_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_hint_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_hint_lbl, 380);
    lv_label_set_text(s_hint_lbl, "Locating...");
    lv_obj_align(s_hint_lbl, LV_ALIGN_CENTER, 0, -75);

    // ── level lens (bottom half) ──
    s_level_lens = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_level_lens);
    lv_obj_set_size(s_level_lens, LEVEL_R * 2, LEVEL_R * 2);
    lv_obj_align(s_level_lens, LV_ALIGN_CENTER, 0, LEVEL_CY);
    lv_obj_set_style_radius(s_level_lens, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_level_lens, lv_color_hex(0x1F1308), 0);
    lv_obj_set_style_bg_opa(s_level_lens, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_level_lens, lv_color_hex(0x6A4A2C), 0);
    lv_obj_set_style_border_width(s_level_lens, 2, 0);
    lv_obj_remove_flag(s_level_lens, LV_OBJ_FLAG_SCROLLABLE);

    s_crosshair_h = lv_obj_create(s_level_lens);
    lv_obj_remove_style_all(s_crosshair_h);
    lv_obj_set_size(s_crosshair_h, LEVEL_R * 2 - 14, 1);
    lv_obj_set_style_bg_color(s_crosshair_h, lv_color_hex(0x4A3828), 0);
    lv_obj_set_style_bg_opa(s_crosshair_h, LV_OPA_COVER, 0);
    lv_obj_align(s_crosshair_h, LV_ALIGN_CENTER, 0, 0);

    s_crosshair_v = lv_obj_create(s_level_lens);
    lv_obj_remove_style_all(s_crosshair_v);
    lv_obj_set_size(s_crosshair_v, 1, LEVEL_R * 2 - 14);
    lv_obj_set_style_bg_color(s_crosshair_v, lv_color_hex(0x4A3828), 0);
    lv_obj_set_style_bg_opa(s_crosshair_v, LV_OPA_COVER, 0);
    lv_obj_align(s_crosshair_v, LV_ALIGN_CENTER, 0, 0);

    s_level_bubble = lv_obj_create(s_level_lens);
    lv_obj_remove_style_all(s_level_bubble);
    lv_obj_set_size(s_level_bubble, BUBBLE_R * 2, BUBBLE_R * 2);
    lv_obj_set_style_radius(s_level_bubble, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_level_bubble, lv_color_hex(0xE6A050), 0);
    lv_obj_set_style_bg_opa(s_level_bubble, LV_OPA_COVER, 0);
    lv_obj_align(s_level_bubble, LV_ALIGN_CENTER, 0, 0);

    s_level_text = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_level_text, &mont_light_14, 0);
    lv_obj_set_style_text_color(s_level_text, lv_color_hex(0x9C7848), 0);
    lv_label_set_text(s_level_text, "--° / --°");
    lv_obj_align(s_level_text, LV_ALIGN_CENTER, 0, LEVEL_CY + LEVEL_R + 14);

    // Location footer
    s_loc_lbl = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_loc_lbl, &mont_light_14, 0);
    lv_obj_set_style_text_color(s_loc_lbl, lv_color_hex(0x705840), 0);
    lv_label_set_text(s_loc_lbl, "--");
    lv_obj_align(s_loc_lbl, LV_ALIGN_CENTER, 0, 220);

    // One-shot location lookup. This may block briefly on first run.
    s_has_loc = geo::getLocation(&s_loc);
    if (!s_has_loc) {
        lv_label_set_text(s_hint_lbl,
            "No WiFi / no cached location.\nConnect & re-open.");
    }

    s_last_tick_sec = 0;     // force first tick to render
}

void leave() {
    if (s_root) { lv_obj_clean(s_root); s_root = nullptr; }
    s_az_lbl = s_dir_lbl = s_elev_lbl = nullptr;
    s_hint_lbl = s_loc_lbl = s_status_lbl = nullptr;
    s_level_lens = s_level_bubble = s_level_text = nullptr;
    s_crosshair_h = s_crosshair_v = nullptr;
}

void tick() {
    if (!s_root) return;

    // ── level (high frequency, ~10Hz) ──────────────────────────────────
    uint32_t now_ms = millis();
    if (s_level_bubble && now_ms - s_last_imu_ms >= 100 && M5.Imu.isEnabled()) {
        s_last_imu_ms = now_ms;
        M5.Imu.update();
        float ax, ay, az;
        M5.Imu.getAccel(&ax, &ay, &az);
        float roll  = atan2f(ay, az) * 180.0f / (float)M_PI;
        float pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * 180.0f / (float)M_PI;
        int bx = (int)(roll * DEG_TO_PX);
        int by = (int)(-pitch * DEG_TO_PX);
        int max_off = LEVEL_R - BUBBLE_R - 4;
        int r2 = bx * bx + by * by;
        int max_r2 = max_off * max_off;
        if (r2 > max_r2) {
            float k = (float)max_off / sqrtf((float)r2);
            bx = (int)(bx * k); by = (int)(by * k);
        }
        lv_obj_align(s_level_bubble, LV_ALIGN_CENTER, bx, by);
        bool is_level = fabsf(roll) < LEVEL_TH && fabsf(pitch) < LEVEL_TH;
        lv_obj_set_style_bg_color(s_level_bubble,
            lv_color_hex(is_level ? 0x80D070 : 0xE6A050), 0);
        if (s_level_text) {
            char lbuf[24];
            snprintf(lbuf, sizeof(lbuf), "%+.0f° / %+.0f°", roll, pitch);
            lv_label_set_text(s_level_text, lbuf);
            lv_obj_set_style_text_color(s_level_text,
                lv_color_hex(is_level ? 0x80D070 : 0x9C7848), 0);
        }
    }

    // ── sun position (1Hz, gated on having a location) ─────────────────
    if (!s_has_loc) return;
    time_t now = time(nullptr);
    if ((uint32_t)now == s_last_tick_sec) return;
    s_last_tick_sec = (uint32_t)now;

    sun::Pos p = sun::positionAt(s_loc.lat, s_loc.lon, now);

    char buf[80];
    snprintf(buf, sizeof(buf), "%d°", (int)(p.azimuth + 0.5f));
    lv_label_set_text(s_az_lbl, buf);
    lv_label_set_text(s_dir_lbl, eight_way(p.azimuth));

    snprintf(buf, sizeof(buf), "elevation %+d°", (int)(p.elevation + 0.5f));
    lv_label_set_text(s_elev_lbl, buf);
    lv_obj_set_style_text_color(s_elev_lbl,
        lv_color_hex(p.elevation < 0 ? 0xC04030 : 0xCFC2A8), 0);

    if (p.elevation < 0) {
        lv_label_set_text(s_hint_lbl,
            "Sun is below the horizon.\nWait for sunrise.");
    } else {
        int n_clock = north_clock_position(p.azimuth);
        snprintf(buf, sizeof(buf),
                 "Point the 12 at the sun.\nNorth is at %d o'clock.",
                 n_clock);
        lv_label_set_text(s_hint_lbl, buf);
    }

    snprintf(buf, sizeof(buf), "%s   %.1f°%c %.1f°%c",
             s_loc.city,
             fabsf(s_loc.lat), s_loc.lat >= 0 ? 'N' : 'S',
             fabsf(s_loc.lon), s_loc.lon >= 0 ? 'E' : 'W');
    lv_label_set_text(s_loc_lbl, buf);
}

}  // namespace app_sundial
