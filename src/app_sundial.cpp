#include "app_sundial.h"
#include "sd_ui/sd_assets.h"
#include "sun.h"
#include "geo.h"
#include <lvgl.h>
#include <M5Unified.h>
#include <Arduino.h>
#include <time.h>
#include <stdio.h>
#include <math.h>

namespace app_sundial {

// Mechanical sundial: a brass dial (static, in sd_bg) with a live solar-shadow
// line the user aligns with the real sun, plus a centre bubble level so the
// watch is held flat while sighting. Sun azimuth/elevation from sun::positionAt;
// tilt from the IMU.
static lv_obj_t* s_root        = nullptr;
static lv_obj_t* s_shadow      = nullptr;   // rotates by sun azimuth
static lv_obj_t* s_bubble      = nullptr;   // moves with tilt
static lv_obj_t* s_status_lbl  = nullptr;   // on the bottom ribbon plate

static constexpr int   CX = 233, CY = 233;          // screen centre
static constexpr int   SHADOW_PIVOT = 17;           // line's rotation axis (local)
static constexpr int   BUBBLE_HALF  = 28;           // 56/2
static constexpr int   BUBBLE_MAXOFF = 34;          // keep bubble inside the lens
static constexpr float TILT_GAIN   = 1.6f;          // deg → px
static constexpr float LEVEL_TH    = 2.5f;          // deg considered "level"

static geo::Location s_loc = {0, 0, {0}};
static bool      s_has_loc = false;
static uint32_t  s_last_imu_ms = 0;
static uint32_t  s_last_tick_sec = 0;

static const char* eight_way(float deg) {
    static const char* labels[16] = {
        "N","NNE","NE","ENE","E","ESE","SE","SSE",
        "S","SSW","SW","WSW","W","WNW","NW","NNW"};
    int idx = (int)((deg + 11.25f) / 22.5f) % 16;
    if (idx < 0) idx += 16;
    return labels[idx];
}

void enter(lv_obj_t* parent) {
    s_root = parent;
    lv_obj_set_style_bg_color(s_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    // Layer 1 — full static brass dial (dial + letters + ring + gnomon + ribbon
    // plate + glass reflection, all pre-composited).
    lv_obj_t* bg = lv_image_create(s_root);
    lv_image_set_src(bg, &sd_bg);
    lv_obj_set_pos(bg, 0, 0);

    // Layer 5 — solar shadow line; pivot at its inner axis aligned to centre.
    s_shadow = lv_image_create(s_root);
    lv_image_set_src(s_shadow, &sd_shadow);
    lv_obj_set_pos(s_shadow, CX - SHADOW_PIVOT, CY - SHADOW_PIVOT);
    lv_image_set_pivot(s_shadow, SHADOW_PIVOT, SHADOW_PIVOT);

    // Layer 6 — level lens liquid (under the bubble).
    lv_obj_t* lens = lv_image_create(s_root);
    lv_image_set_src(lens, &sd_bubble_base);
    lv_obj_set_pos(lens, CX - 66, CY - 66);

    // Layer 7 — the air bubble (moves with tilt).
    s_bubble = lv_image_create(s_root);
    lv_image_set_src(s_bubble, &sd_bubble);
    lv_obj_set_pos(s_bubble, CX - BUBBLE_HALF, CY - BUBBLE_HALF);

    // Layer 8 — glass highlight over the bubble.
    lv_obj_t* hi = lv_image_create(s_root);
    lv_image_set_src(hi, &sd_bubble_hi);
    lv_obj_set_pos(hi, CX - 66, CY - 66);

    // Layer 9 — status text on the ribbon plate (plate itself is in sd_bg).
    s_status_lbl = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_status_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0xF2D79A), 0);
    lv_label_set_text(s_status_lbl, "Locating...");
    lv_obj_align(s_status_lbl, LV_ALIGN_TOP_MID, 0, 354);

    s_has_loc = geo::getLocation(&s_loc);
    if (!s_has_loc) lv_label_set_text(s_status_lbl, "NO LOCATION");

    s_last_tick_sec = 0;
}

void leave() {
    if (s_root) { lv_obj_clean(s_root); s_root = nullptr; }
    s_shadow = s_bubble = s_status_lbl = nullptr;
}

void tick() {
    if (!s_root) return;

    // ── bubble level (~10 Hz) ──────────────────────────────────────────────
    bool is_level = false;
    uint32_t now_ms = millis();
    if (s_bubble && now_ms - s_last_imu_ms >= 100 && M5.Imu.isEnabled()) {
        s_last_imu_ms = now_ms;
        M5.Imu.update();
        float ax, ay, az;
        M5.Imu.getAccel(&ax, &ay, &az);
        float roll  = atan2f(ay, az) * 180.0f / (float)M_PI;
        float pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * 180.0f / (float)M_PI;
        int bx = (int)( roll  * TILT_GAIN);
        int by = (int)(-pitch * TILT_GAIN);
        int r2 = bx * bx + by * by, maxr2 = BUBBLE_MAXOFF * BUBBLE_MAXOFF;
        if (r2 > maxr2) {
            float k = (float)BUBBLE_MAXOFF / sqrtf((float)r2);
            bx = (int)(bx * k); by = (int)(by * k);
        }
        lv_obj_set_pos(s_bubble, CX - BUBBLE_HALF + bx, CY - BUBBLE_HALF + by);
        is_level = fabsf(roll) < LEVEL_TH && fabsf(pitch) < LEVEL_TH;
        // subtle green glow when flat
        lv_obj_set_style_image_recolor(s_bubble, lv_color_hex(0x7CFF6A), 0);
        lv_obj_set_style_image_recolor_opa(s_bubble, is_level ? 90 : 0, 0);
    }

    // ── sun position (1 Hz) ────────────────────────────────────────────────
    if (!s_has_loc) return;
    time_t now = time(nullptr);
    if ((uint32_t)now == s_last_tick_sec) return;
    s_last_tick_sec = (uint32_t)now;

    sun::Pos p = sun::positionAt(s_loc.lat, s_loc.lon, now);

    // Rotate the shadow line to the sun azimuth (0°=N=up; LVGL 0°=line points
    // east, so subtract 90°). Dim it when the sun is down.
    bool sun_up = p.elevation >= 0;
    lv_image_set_rotation(s_shadow, (int)((p.azimuth - 90.0f) * 10.0f));
    lv_obj_set_style_image_opa(s_shadow, sun_up ? LV_OPA_COVER : 76, 0);

    char buf[64];
    if (!sun_up) {
        snprintf(buf, sizeof(buf), "SUN BELOW HORIZON");
    } else if (!is_level) {
        snprintf(buf, sizeof(buf), "LEVEL THE WATCH");
    } else {
        snprintf(buf, sizeof(buf), "SUN %d\xC2\xB0 %s  ALIGN LINE",
                 (int)(p.azimuth + 0.5f), eight_way(p.azimuth));
    }
    lv_label_set_text(s_status_lbl, buf);
}

}  // namespace app_sundial
