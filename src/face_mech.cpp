#include "face_mech.h"
#include <Arduino.h>
#include <M5Unified.h>
#include <lvgl.h>
#include <time.h>
#include <stdlib.h>
#include <math.h>
#include "steps.h"

// Hand sprites from LILYGO; sepia portrait generated locally from a family photo.
LV_IMAGE_DECLARE(ui_img_clockwise_hour_png);
LV_IMAGE_DECLARE(ui_img_clockwise_min_png);
LV_IMAGE_DECLARE(ui_img_clockwise_sec_png);
LV_IMAGE_DECLARE(papa_stanley_sepia);
LV_IMAGE_DECLARE(ui_img_weather_sun_cloud_png);

// Custom-generated Montserrat-Light at three sizes — much lighter weight than
// the bundled lv_font_montserrat_*, gives the mech face a designer-watch feel
// instead of the heavier system-font look. ASCII + ° only, so flash cost is
// modest (~7 / 12 / 25 KB respectively).
LV_FONT_DECLARE(mont_light_14);
LV_FONT_DECLARE(mont_light_20);
LV_FONT_DECLARE(mont_light_24);
LV_FONT_DECLARE(mont_light_32);

namespace face_mech {

static constexpr const char* TZ_SON  = "PST8PDT,M3.2.0,M11.1.0";  // Stanley, PT
static constexpr const char* TZ_PAPA = "CST-8";                    // Beijing, UTC+8 (no DST)
static constexpr int CX = 234;
static constexpr int CY = 234;

// Pivots inside each hand image (in image coords).
static constexpr int HOUR_PX = 9,  HOUR_PY = 78;
static constexpr int MIN_PX  = 9,  MIN_PY  = 125;
static constexpr int SEC_PX  = 15, SEC_PY  = 150;

// 466/397 ≈ 1.174 — same factor that fills the background, applied to hands too
// so they reach the 5-min tick ring instead of stopping short.
static constexpr int HAND_SCALE = 301;
static constexpr int RING_R     = 222;  // dot ring radius (px from center)

static lv_obj_t* s_root          = nullptr;
static lv_obj_t* s_bg            = nullptr;
static lv_obj_t* s_hour          = nullptr;
static lv_obj_t* s_min           = nullptr;
static lv_obj_t* s_sec           = nullptr;
static lv_obj_t* s_pin           = nullptr;
static lv_obj_t* s_papa_lbl      = nullptr;
static lv_obj_t* s_papa_time     = nullptr;
static lv_obj_t* s_son_lbl       = nullptr;
static lv_obj_t* s_son_time      = nullptr;
static lv_obj_t* s_weather_icon  = nullptr;
static lv_obj_t* s_weather_temp  = nullptr;
static lv_obj_t* s_date          = nullptr;
static lv_obj_t* s_battery       = nullptr;
static lv_obj_t* s_hearts_lbl    = nullptr;
static lv_obj_t* s_steps_lbl     = nullptr;
static lv_obj_t* s_steps_arc     = nullptr;
static lv_obj_t* s_steps_pct     = nullptr;
static lv_obj_t* s_steps_icon    = nullptr;
static int s_unread = 0;

static constexpr int STEPS_GOAL = 5000;

static lv_obj_t* makeHand(lv_obj_t* parent, const lv_image_dsc_t* src, int px, int py) {
    lv_obj_t* img = lv_image_create(parent);
    lv_image_set_src(img, src);
    lv_image_set_pivot(img, px, py);
    lv_image_set_scale(img, HAND_SCALE);
    lv_obj_set_pos(img, CX - px, CY - py);
    return img;
}

// Soft dark halo behind a text label — no visible "plate", just a blurred
// shadow that gives the text some volume / depth against the sepia photo.
// Different from a pill: shadow_width does the blur, so edges feather out
// instead of stopping at a hard rounded rectangle.
static void pillify(lv_obj_t* lbl) {
    lv_obj_set_style_shadow_color(lbl, lv_color_black(), 0);
    lv_obj_set_style_shadow_width(lbl, 10, 0);      // blur radius
    lv_obj_set_style_shadow_spread(lbl, 1, 0);      // tiny expansion
    lv_obj_set_style_shadow_opa(lbl, 130, 0);       // ~51%
    lv_obj_set_style_shadow_offset_x(lbl, 0, 0);    // centered → glow, not drop
    lv_obj_set_style_shadow_offset_y(lbl, 0, 0);
}

void create(lv_obj_t* parent) {
    s_root = parent;
    lv_obj_set_style_bg_color(s_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);

    // ── PAPA & Stanley sepia portrait, fills 466×466 ──
    s_bg = lv_image_create(s_root);
    lv_image_set_src(s_bg, &papa_stanley_sepia);
    lv_obj_align(s_bg, LV_ALIGN_CENTER, 0, 0);

    // ── tick ring: 60 minor white dots, every 5th replaced by big orange ──
    for (int i = 0; i < 60; i++) {
        float a = (i * 6.0f - 90.0f) * 3.14159265f / 180.0f;
        int x = CX + (int)(cosf(a) * RING_R);
        int y = CY + (int)(sinf(a) * RING_R);
        lv_obj_t* d = lv_obj_create(s_root);
        lv_obj_remove_style_all(d);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        if (i % 5 == 0) {
            lv_obj_set_size(d, 9, 9);
            lv_obj_set_style_bg_color(d, lv_color_hex(0xB87838), 0);   // deeper amber (was acid orange)
            lv_obj_set_pos(d, x - 4, y - 4);
        } else {
            lv_obj_set_size(d, 4, 4);
            lv_obj_set_style_bg_color(d, lv_color_hex(0x806848), 0);   // dim sepia-tan (was near-white)
            lv_obj_set_pos(d, x - 2, y - 2);
        }
    }

    // ── PAPA · 北京 digital time (top, 12 o'clock arc) ──
    s_papa_lbl = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_papa_lbl, &mont_light_20, 0);
    lv_obj_set_style_text_color(s_papa_lbl, lv_color_hex(0xE6C46D), 0);
    lv_label_set_text(s_papa_lbl, "PAPA - BJ");
    lv_obj_align(s_papa_lbl, LV_ALIGN_CENTER, 0, -180);

    s_papa_time = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_papa_time, &mont_light_32, 0);
    lv_obj_set_style_text_color(s_papa_time, lv_color_hex(0xF5E8D0), 0);   // cream, not stark white
    lv_label_set_text(s_papa_time, "08:23");
    lv_obj_align(s_papa_time, LV_ALIGN_CENTER, 0, -155);

    // ── STANLEY · PT digital time (bottom, 6 o'clock arc) ──
    s_son_lbl = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_son_lbl, &mont_light_20, 0);
    lv_obj_set_style_text_color(s_son_lbl, lv_color_hex(0xB8D8A0), 0);     // soft warm sage, not neon green
    lv_label_set_text(s_son_lbl, "STANLEY - PT");
    lv_obj_align(s_son_lbl, LV_ALIGN_CENTER, 0, 155);

    s_son_time = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_son_time, &mont_light_32, 0);
    lv_obj_set_style_text_color(s_son_time, lv_color_hex(0xF5E8D0), 0);   // matching cream
    lv_label_set_text(s_son_time, "20:23");
    lv_obj_align(s_son_time, LV_ALIGN_CENTER, 0, 180);

    // ── weather (9 o'clock) ──
    s_weather_icon = lv_image_create(s_root);
    lv_image_set_src(s_weather_icon, &ui_img_weather_sun_cloud_png);
    lv_obj_align(s_weather_icon, LV_ALIGN_CENTER, -160, -15);

    s_weather_temp = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_weather_temp, &mont_light_24, 0);
    lv_obj_set_style_text_color(s_weather_temp, lv_color_hex(0xF5E8D0), 0);
    lv_label_set_text(s_weather_temp, "21°");
    lv_obj_align(s_weather_temp, LV_ALIGN_CENTER, -160, 15);

    // ── date (3 o'clock) ──
    s_date = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_date, &mont_light_24, 0);
    lv_obj_set_style_text_color(s_date, lv_color_hex(0xF5E8D0), 0);
    lv_obj_set_style_text_align(s_date, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_date, "SAT\n30 MAY");
    lv_obj_align(s_date, LV_ALIGN_CENTER, 160, 0);

    // ── battery (top-left small) ──
    // Uses bundled lv_font_montserrat_18 because we need LV_SYMBOL_CHARGE /
    // BATTERY_* glyphs (FontAwesome subset baked into LVGL's montserrat).
    // Slight weight mismatch with our Light family is OK on such a small element.
    s_battery = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_battery, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_battery, lv_color_hex(0xE6A050), 0);
    lv_label_set_text(s_battery, "--%");
    lv_obj_align(s_battery, LV_ALIGN_CENTER, -100, -130);

    // ── steps complication (7 o'clock): arc against goal + count + percent ──
    // Arc: 270° track, opens at 12 o'clock-ish of its own complication face.
    s_steps_arc = lv_arc_create(s_root);
    lv_obj_set_size(s_steps_arc, 92, 92);
    lv_obj_align(s_steps_arc, LV_ALIGN_CENTER, -95, 90);
    lv_arc_set_rotation(s_steps_arc, 135);          // 7:30 start
    lv_arc_set_bg_angles(s_steps_arc, 0, 270);       // 270° span
    lv_arc_set_range(s_steps_arc, 0, 100);
    lv_arc_set_value(s_steps_arc, 0);
    // Track stays visible at 0% so the progress bar reads as a real "ring you
    // need to fill", not just a barely-there hint. Indicator is bright amber
    // against the medium-brown track for clear progress reading.
    lv_obj_set_style_arc_color(s_steps_arc, lv_color_hex(0x7A5840), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_steps_arc, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_steps_arc, lv_color_hex(0xFFC868), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_steps_arc, 7, LV_PART_INDICATOR);
    lv_obj_remove_style(s_steps_arc, NULL, LV_PART_KNOB);    // no draggable knob
    lv_obj_remove_flag(s_steps_arc, LV_OBJ_FLAG_CLICKABLE);

    // FontAwesome shoe-prints glyph (U+F54B) as the complication header,
    // sitting just above the arc so it identifies what the ring is counting
    // without crowding the digits inside. UTF-8 is 0xEF 0x95 0x8B.
    s_steps_icon = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_steps_icon, &mont_light_24, 0);
    lv_obj_set_style_text_color(s_steps_icon, lv_color_hex(0xB88860), 0);
    lv_label_set_text(s_steps_icon, "\xEF\x95\x8B");
    lv_obj_align(s_steps_icon, LV_ALIGN_CENTER, -95, 14);

    s_steps_lbl = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_steps_lbl, &mont_light_24, 0);
    lv_obj_set_style_text_color(s_steps_lbl, lv_color_hex(0xF0B080), 0);
    lv_label_set_text(s_steps_lbl, "0");
    lv_obj_align(s_steps_lbl, LV_ALIGN_CENTER, -95, 85);

    // "/ 5000" denominator under the count, so the goal is always visible.
    s_steps_pct = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_steps_pct, &mont_light_14, 0);
    lv_obj_set_style_text_color(s_steps_pct, lv_color_hex(0xB88860), 0);
    lv_label_set_text(s_steps_pct, "/ 5000");
    lv_obj_align(s_steps_pct, LV_ALIGN_CENTER, -95, 104);

    // ── hearts (bottom-right, 5 o'clock) ──
    // Uses bundled montserrat for LV_SYMBOL_BELL — our mont_light_24 only
    // ships ASCII+°, so the bell would otherwise render as a tofu box.
    s_hearts_lbl = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_hearts_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_hearts_lbl, lv_color_hex(0xE07090), 0);  // dusty rose, not hot pink
    lv_label_set_text(s_hearts_lbl, LV_SYMBOL_BELL "  0");
    lv_obj_align(s_hearts_lbl, LV_ALIGN_CENTER, 110, 105);

    // (no pillify — dark halo made the face feel "dirty". Text integrates
    //  with the sepia bg via warm color harmony instead. See color choices
    //  above: cream/amber/peach replace stark white/cyan/lime-green.)

    // ── hands (back-to-front) ──
    s_hour = makeHand(s_root, &ui_img_clockwise_hour_png, HOUR_PX, HOUR_PY);
    s_min  = makeHand(s_root, &ui_img_clockwise_min_png,  MIN_PX,  MIN_PY);
    s_sec  = makeHand(s_root, &ui_img_clockwise_sec_png,  SEC_PX,  SEC_PY);

    // Recolor the second hand from LILYGO's green to brass amber so it sits
    // in the same warm-metal family as the center cap and 5-min orange ticks.
    // image_recolor with full opa replaces the source color but keeps alpha,
    // so the shape stays sharp.
    lv_obj_set_style_image_recolor(s_sec, lv_color_hex(0xE6C46D), 0);
    lv_obj_set_style_image_recolor_opa(s_sec, LV_OPA_COVER, 0);

    // ── center cap ──
    s_pin = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_pin);
    lv_obj_set_size(s_pin, 14, 14);
    lv_obj_set_style_radius(s_pin, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_pin, lv_color_hex(0xE6C46D), 0);
    lv_obj_set_style_bg_opa(s_pin, LV_OPA_COVER, 0);
    lv_obj_align(s_pin, LV_ALIGN_CENTER, 0, 0);

    update();
}

void setUnread(int n) {
    s_unread = n;
    if (s_hearts_lbl) {
        char buf[16];
        snprintf(buf, sizeof(buf), LV_SYMBOL_BELL "  %d", n);
        lv_label_set_text(s_hearts_lbl, buf);
    }
}

void update() {
    if (!s_hour) return;

    // Stanley's time (PT) drives the analog hands.
    setenv("TZ", TZ_SON, 1); tzset();
    time_t now = time(nullptr);
    struct tm t; localtime_r(&now, &t);

    int h = t.tm_hour % 12, m = t.tm_min, s = t.tm_sec;
    lv_image_set_rotation(s_hour, (h * 300) + (m * 5));
    lv_image_set_rotation(s_min,  m * 60);
    lv_image_set_rotation(s_sec,  s * 60);

    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    lv_label_set_text(s_son_time, buf);

    static const char* days[]   = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
    static const char* months[] = {"JAN","FEB","MAR","APR","MAY","JUN",
                                   "JUL","AUG","SEP","OCT","NOV","DEC"};
    snprintf(buf, sizeof(buf), "%s\n%d %s",
             days[t.tm_wday & 7], t.tm_mday, months[t.tm_mon % 12]);
    lv_label_set_text(s_date, buf);

    // PAPA Beijing time (world-clock complication).
    setenv("TZ", TZ_PAPA, 1); tzset();
    struct tm tp; localtime_r(&now, &tp);
    snprintf(buf, sizeof(buf), "%02d:%02d", tp.tm_hour, tp.tm_min);
    lv_label_set_text(s_papa_time, buf);

    // ── battery: charging state changes both icon and tint ──
    int  bat      = M5.Power.getBatteryLevel();
    bool charging = M5.Power.isCharging();
    const char* bat_icon;
    if (charging)         bat_icon = LV_SYMBOL_CHARGE;        // ⚡ on USB
    else if (bat < 0)     bat_icon = LV_SYMBOL_USB;
    else if (bat < 20)    bat_icon = LV_SYMBOL_BATTERY_EMPTY;
    else if (bat < 40)    bat_icon = LV_SYMBOL_BATTERY_1;
    else if (bat < 60)    bat_icon = LV_SYMBOL_BATTERY_2;
    else if (bat < 80)    bat_icon = LV_SYMBOL_BATTERY_3;
    else                  bat_icon = LV_SYMBOL_BATTERY_FULL;
    lv_obj_set_style_text_color(s_battery,
        charging ? lv_color_hex(0xF0C870) : lv_color_hex(0xE6A050), 0);
    if (bat < 0) snprintf(buf, sizeof(buf), "%s --", bat_icon);
    else         snprintf(buf, sizeof(buf), "%s %d%%", bat_icon, bat);
    lv_label_set_text(s_battery, buf);

    // ── steps: count + arc against 5000/day goal ──
    // Once the goal is hit, arc stays full but the indicator shifts to a
    // brighter "celebration gold" — same for the digit color. Reads as
    // "good job, keep going!" instead of forcing the kid to grok "150%".
    int s_today  = steps::today();
    bool reached = s_today >= STEPS_GOAL;
    int pct = s_today * 100 / STEPS_GOAL;
    if (pct > 100) pct = 100;
    lv_arc_set_value(s_steps_arc, pct);
    snprintf(buf, sizeof(buf), "%d", s_today);
    lv_label_set_text(s_steps_lbl, buf);
    lv_obj_set_style_arc_color(s_steps_arc,
        reached ? lv_color_hex(0xFFE070) : lv_color_hex(0xFFC868),
        LV_PART_INDICATOR);
    lv_obj_set_style_text_color(s_steps_lbl,
        reached ? lv_color_hex(0xFFE070) : lv_color_hex(0xF0B080), 0);
}

void destroy() {
    s_root = s_bg = s_hour = s_min = s_sec = s_pin = nullptr;
    s_weather_icon = s_weather_temp = nullptr;
    s_date = s_battery = s_hearts_lbl = s_steps_lbl = nullptr;
    s_steps_arc = s_steps_pct = s_steps_icon = nullptr;
    s_papa_lbl = s_papa_time = s_son_lbl = s_son_time = nullptr;
}

}  // namespace face_mech
