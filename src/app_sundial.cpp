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
// Daylight sundial — cream dial, dark text/ticks. A small "matchstick"
// gnomon stands at the 12-o'clock position; a semi-transparent dark line
// extends from its base across the dial past the centre, simulating the
// shadow cast by the sun (which the user aligns with the actual sun).
static lv_obj_t* s_dial_ring     = nullptr;
static lv_obj_t* s_shadow        = nullptr;
static lv_obj_t* s_gnomon_head   = nullptr;
static lv_obj_t* s_gnomon_stick  = nullptr;
static lv_obj_t* s_card_lbl[4]   = {};      // N, E, S, W
static lv_obj_t* s_tick[12]      = {};      // 12 dots, every 30°, rotate w/ card
static lv_obj_t* s_status_lbl    = nullptr;
static lv_obj_t* s_dir_lbl       = nullptr;
static lv_obj_t* s_loc_lbl       = nullptr;
// Level lens (bottom half)
static lv_obj_t* s_level_lens    = nullptr;
static lv_obj_t* s_level_bubble  = nullptr;
static lv_obj_t* s_level_text    = nullptr;
static lv_obj_t* s_crosshair_h   = nullptr;
static lv_obj_t* s_crosshair_v   = nullptr;

static uint32_t s_last_imu_ms = 0;

// Full-screen compass: dial fills the panel so the user can read it at arm's
// length outdoors. Tiny bubble level sits at the very centre (real handheld
// compasses do this) so the watch's flatness is verified while sighting.
static constexpr int DIAL_CY      = 0;
static constexpr int DIAL_R       = 205;     // outer ring near panel edge
static constexpr int TICK_R       = 188;
static constexpr int LETTER_R     = 152;
static constexpr int LEVEL_CY     = 0;
static constexpr int LEVEL_R      = 34;      // tiny centre bubble lens
static constexpr int BUBBLE_R     = 10;
static constexpr float DEG_TO_PX  = 1.1f;
static constexpr float LEVEL_TH   = 2.5f;

static const char* CARD_LETTERS[4] = {"N", "E", "S", "W"};
// On the cream dial, N stays red; the other three are dark sepia.
static const uint32_t CARD_COLORS[4] = {0xC02828, 0x3A2818, 0x3A2818, 0x3A2818};

// Palette for the light/parchment dial
static constexpr uint32_t COL_PAPER   = 0xE6D2A8;   // warm cream
static constexpr uint32_t COL_INK     = 0x3A2818;   // dark sepia (border + text)
static constexpr uint32_t COL_TICK_M  = 0x3A2818;   // major tick
static constexpr uint32_t COL_TICK_S  = 0x7A5840;   // minor tick
static constexpr uint32_t COL_AMBER   = 0xC8782C;   // gnomon head (sun)
static constexpr uint32_t COL_SHADOW  = 0x3A2818;   // shadow line
static constexpr uint32_t COL_BUBBLE  = 0xC02828;   // spirit-level bubble red
static constexpr uint32_t COL_LEVEL_OK= 0x4F8830;   // bubble centred = green

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

    // ── parchment dial face (filled) ──
    s_dial_ring = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_dial_ring);
    lv_obj_set_size(s_dial_ring, DIAL_R * 2, DIAL_R * 2);
    lv_obj_align(s_dial_ring, LV_ALIGN_CENTER, 0, DIAL_CY);
    lv_obj_set_style_radius(s_dial_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_dial_ring, lv_color_hex(COL_PAPER), 0);
    lv_obj_set_style_bg_opa(s_dial_ring, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_dial_ring, lv_color_hex(COL_INK), 0);
    lv_obj_set_style_border_width(s_dial_ring, 5, 0);     // thicker frame
    lv_obj_remove_flag(s_dial_ring, LV_OBJ_FLAG_SCROLLABLE);

    // ── gnomon shadow — thicker, darker, more like real cast shadow ──
    {
        const int top = -DIAL_R + 24;
        const int bot = 30;
        s_shadow = lv_obj_create(s_root);
        lv_obj_remove_style_all(s_shadow);
        lv_obj_set_size(s_shadow, 9, bot - top);            // 4 → 9 px wide
        lv_obj_set_style_bg_color(s_shadow, lv_color_hex(COL_SHADOW), 0);
        lv_obj_set_style_bg_opa(s_shadow, 160, 0);          // ~63% opacity
        lv_obj_set_style_radius(s_shadow, 2, 0);
        lv_obj_align(s_shadow, LV_ALIGN_CENTER, 0, (top + bot) / 2);
    }

    // ── 12 tick dots — fatter, more compass-like ──
    for (int i = 0; i < 12; i++) {
        s_tick[i] = lv_obj_create(s_root);
        lv_obj_remove_style_all(s_tick[i]);
        bool major = (i % 3 == 0);
        int sz = major ? 12 : 7;                            // 7/4 → 12/7
        lv_obj_set_size(s_tick[i], sz, sz);
        lv_obj_set_style_radius(s_tick[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_tick[i],
            lv_color_hex(major ? COL_TICK_M : COL_TICK_S), 0);
        lv_obj_set_style_bg_opa(s_tick[i], LV_OPA_COVER, 0);
        lv_obj_align(s_tick[i], LV_ALIGN_CENTER, 0, DIAL_CY);
    }

    // ── 4 cardinal letters — bundled montserrat (Medium weight) is much
    //    chunkier than our generated Mont Light; reads "compass card" not
    //    "hairline label" outdoors.
    for (int i = 0; i < 4; i++) {
        s_card_lbl[i] = lv_label_create(s_root);
        lv_obj_set_style_text_font(s_card_lbl[i], &lv_font_montserrat_32, 0);
        lv_obj_set_style_text_color(s_card_lbl[i], lv_color_hex(CARD_COLORS[i]), 0);
        lv_label_set_text(s_card_lbl[i], CARD_LETTERS[i]);
        lv_obj_align(s_card_lbl[i], LV_ALIGN_CENTER, 0, DIAL_CY);
    }

    // ── gnomon stick + amber sun head, both beefier ──
    s_gnomon_stick = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_gnomon_stick);
    lv_obj_set_size(s_gnomon_stick, 7, 22);                 // 5×20 → 7×22
    lv_obj_set_style_radius(s_gnomon_stick, 2, 0);
    lv_obj_set_style_bg_color(s_gnomon_stick, lv_color_hex(COL_INK), 0);
    lv_obj_set_style_bg_opa(s_gnomon_stick, LV_OPA_COVER, 0);
    lv_obj_align(s_gnomon_stick, LV_ALIGN_CENTER, 0, -DIAL_R + 15);

    s_gnomon_head = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_gnomon_head);
    lv_obj_set_size(s_gnomon_head, 22, 22);                 // 16 → 22
    lv_obj_set_style_radius(s_gnomon_head, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_gnomon_head, lv_color_hex(COL_AMBER), 0);
    lv_obj_set_style_bg_opa(s_gnomon_head, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(s_gnomon_head, lv_color_hex(COL_AMBER), 0);
    lv_obj_set_style_shadow_width(s_gnomon_head, 12, 0);
    lv_obj_set_style_shadow_opa(s_gnomon_head, 130, 0);
    lv_obj_set_style_shadow_spread(s_gnomon_head, 0, 0);
    lv_obj_align(s_gnomon_head, LV_ALIGN_CENTER, 0, -DIAL_R - 5);

    // Status line — Medium-weight bundled montserrat for legibility outdoors.
    s_status_lbl = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_status_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(COL_INK), 0);
    lv_label_set_text(s_status_lbl, "Locating...");
    lv_obj_align(s_status_lbl, LV_ALIGN_CENTER, 0, 65);

    s_dir_lbl = nullptr;

    // ── centre bubble lens (lives at the END of the shadow line) ──
    // Lens body is parchment so the shadow appears to terminate at it;
    // border is dark ink for a real-compass look.
    s_level_lens = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_level_lens);
    lv_obj_set_size(s_level_lens, LEVEL_R * 2, LEVEL_R * 2);
    lv_obj_align(s_level_lens, LV_ALIGN_CENTER, 0, LEVEL_CY);
    lv_obj_set_style_radius(s_level_lens, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_level_lens, lv_color_hex(COL_PAPER), 0);
    lv_obj_set_style_bg_opa(s_level_lens, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_level_lens, lv_color_hex(COL_INK), 0);
    lv_obj_set_style_border_width(s_level_lens, 2, 0);
    lv_obj_remove_flag(s_level_lens, LV_OBJ_FLAG_SCROLLABLE);

    s_crosshair_h = s_crosshair_v = nullptr;

    s_level_bubble = lv_obj_create(s_level_lens);
    lv_obj_remove_style_all(s_level_bubble);
    lv_obj_set_size(s_level_bubble, BUBBLE_R * 2, BUBBLE_R * 2);
    lv_obj_set_style_radius(s_level_bubble, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_level_bubble, lv_color_hex(COL_BUBBLE), 0);
    lv_obj_set_style_bg_opa(s_level_bubble, LV_OPA_COVER, 0);
    lv_obj_align(s_level_bubble, LV_ALIGN_CENTER, 0, 0);

    s_level_text = nullptr;

    // Location footer — Medium-weight bundled font, dark sepia on parchment
    s_loc_lbl = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_loc_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_loc_lbl, lv_color_hex(COL_INK), 0);
    lv_label_set_text(s_loc_lbl, "--");
    lv_obj_align(s_loc_lbl, LV_ALIGN_CENTER, 0, 105);

    // One-shot location lookup. Cached at boot, so this returns instantly
    // after the first session. If still nothing (no WiFi and no cache),
    // surface the failure in the status row.
    s_has_loc = geo::getLocation(&s_loc);
    if (!s_has_loc) {
        lv_label_set_text(s_status_lbl, "No WiFi / no cached location");
    }

    s_last_tick_sec = 0;     // force first tick to render
}

void leave() {
    if (s_root) { lv_obj_clean(s_root); s_root = nullptr; }
    s_status_lbl = s_dir_lbl = s_loc_lbl = nullptr;
    s_dial_ring = s_shadow = s_gnomon_head = s_gnomon_stick = nullptr;
    for (int i = 0; i < 4; i++)  s_card_lbl[i] = nullptr;
    for (int i = 0; i < 12; i++) s_tick[i]     = nullptr;
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
            lv_color_hex(is_level ? COL_LEVEL_OK : COL_BUBBLE), 0);
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

    // ── place ticks + cardinals on the dial ──────────────────────────────
    // Watch's 12-o'clock = sun direction. A real-world direction D appears
    // on the dial at clock-angle (D − azimuth) clockwise from 12.
    // Screen offset: x = R·sin(θ), y = −R·cos(θ).
    for (int i = 0; i < 12; i++) {
        if (!s_tick[i]) continue;
        float theta = (i * 30.0f - p.azimuth) * (float)M_PI / 180.0f;
        int dx = (int)( TICK_R * sinf(theta));
        int dy = (int)(-TICK_R * cosf(theta));
        lv_obj_align(s_tick[i], LV_ALIGN_CENTER, dx, DIAL_CY + dy);
    }
    for (int i = 0; i < 4; i++) {
        if (!s_card_lbl[i]) continue;
        float theta = (i * 90.0f - p.azimuth) * (float)M_PI / 180.0f;
        int dx = (int)( LETTER_R * sinf(theta));
        int dy = (int)(-LETTER_R * cosf(theta));
        lv_obj_align(s_card_lbl[i], LV_ALIGN_CENTER, dx, DIAL_CY + dy);
    }

    char buf[64];
    if (p.elevation < 0) {
        snprintf(buf, sizeof(buf), "Sun below horizon - wait for sunrise");
        lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0xA02020), 0);
    } else {
        snprintf(buf, sizeof(buf), "%d° %s   elev %+d°",
                 (int)(p.azimuth + 0.5f),
                 eight_way(p.azimuth),
                 (int)(p.elevation + 0.5f));
        lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(COL_INK), 0);
    }
    lv_label_set_text(s_status_lbl, buf);

    // Just the city name now — full-screen dial leaves no room for lat/lon
    // and the user can dig that out of Settings if needed.
    lv_label_set_text(s_loc_lbl, s_loc.city);
}

}  // namespace app_sundial
