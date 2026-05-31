// face_lcd v3 — dense Casio-style LCD.
// Layout zones on the round 468×468 panel:
//   top bezel        "STANLEY" branding
//   weekday strip    SU MO TU WE TH FR SA  (current day highlighted)
//   indicator row    ALM ▶                AM   PM
//   hero time        88:88 ghost + HH:MM live (DSEG7 100)
//   sub-readouts     :SS seconds (DSEG7 36) | MAY 29 date (DSEG14 28)
//   dad row          DAD PEK + HH:MM live  (dual-time mode)
//   bottom bezel     "DUAL TIME WORLD"

#include "face_lcd.h"
#include "dad_status.h"
#include "tz_helper.h"
#include <Arduino.h>
#include <lvgl.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <cstring>

LV_FONT_DECLARE(dseg7_modern_100);
LV_FONT_DECLARE(dseg7_modern_36);
LV_FONT_DECLARE(dseg14_modern_20);
LV_FONT_DECLARE(dseg14_modern_28);

namespace face_lcd {

static constexpr const char* TZ_SON = "PST8PDT,M3.2.0,M11.1.0";
static constexpr const char* TZ_DAD = "CST-8";

// phosphor LCD palette
static constexpr uint32_t COL_BG       = 0x021505;
static constexpr uint32_t COL_GHOST    = 0x0E2C1A;   // ghost segments
static constexpr uint32_t COL_RIM      = 0x144A2A;   // bezel text very dim
static constexpr uint32_t COL_DIM      = 0x2D8050;   // unlit labels / annotation
static constexpr uint32_t COL_LIT      = 0x42FF7E;   // active phosphor
static constexpr uint32_t COL_HIGHLITE = 0x90FFA6;   // brightest highlight

// widgets
static lv_obj_t* s_scr        = nullptr;
static lv_obj_t* s_brand_top  = nullptr;
static lv_obj_t* s_days[7]    = {};
static lv_obj_t* s_alm        = nullptr;
static lv_obj_t* s_am         = nullptr;
static lv_obj_t* s_pm         = nullptr;
static lv_obj_t* s_time_ghost = nullptr;
static lv_obj_t* s_time_live  = nullptr;
static lv_obj_t* s_sec_ghost  = nullptr;
static lv_obj_t* s_sec_live   = nullptr;
static lv_obj_t* s_date       = nullptr;
static lv_obj_t* s_div_line   = nullptr;
static lv_obj_t* s_dad_lbl    = nullptr;
static lv_obj_t* s_dad_time   = nullptr;
static lv_obj_t* s_brand_bot  = nullptr;

// status indicators (top of face)
static lv_obj_t* s_wifi_dot   = nullptr;
static lv_obj_t* s_mqtt_dot   = nullptr;
static lv_obj_t* s_unread_lbl = nullptr;

// dad-state line below the dad row
static lv_obj_t* s_dad_state  = nullptr;

// alert state (heart sent / received)
static uint32_t  s_alert_until    = 0;
static uint32_t  s_alert_color    = 0;
static char      s_alert_text[40] = {};

static lv_obj_t* mk_label(const char* text, const lv_font_t* font,
                          uint32_t color, lv_align_t align, int xo, int yo) {
    lv_obj_t* l = lv_label_create(s_scr);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, text);
    lv_obj_align(l, align, xo, yo);
    return l;
}

void create(lv_obj_t* parent) {
    s_scr = parent;
    lv_obj_clean(s_scr);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);

    // ── top bezel: STANLEY ──
    s_brand_top = mk_label("STANLEY", &dseg14_modern_20, COL_RIM,
                           LV_ALIGN_TOP_MID, 0, 24);

    // ── status dots (left of bezel): WiFi and MQTT ──
    auto mk_dot = [&](int xo, int yo) {
        lv_obj_t* d = lv_obj_create(s_scr);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 11, 11);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(d, lv_color_hex(COL_GHOST), 0);
        lv_obj_align(d, LV_ALIGN_TOP_MID, xo, yo);
        return d;
    };
    s_wifi_dot = mk_dot(-66, 28);
    s_mqtt_dot = mk_dot(-52, 28);

    // ── unread message badge (right of bezel) ──
    s_unread_lbl = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_unread_lbl, &dseg14_modern_20, 0);
    lv_obj_set_style_text_color(s_unread_lbl, lv_color_hex(0xFF6FAE), 0);
    lv_label_set_text(s_unread_lbl, "");
    lv_obj_align(s_unread_lbl, LV_ALIGN_TOP_MID, 56, 24);

    // ── weekday strip ──
    const char* wd[7] = {"SU","MO","TU","WE","TH","FR","SA"};
    const int strip_y = 60;
    const int cell_w  = 47;
    const int strip_start = (468 - cell_w * 7) / 2;
    for (int i = 0; i < 7; i++) {
        s_days[i] = lv_label_create(s_scr);
        lv_obj_set_style_text_font(s_days[i], &dseg14_modern_20, 0);
        lv_obj_set_style_text_color(s_days[i], lv_color_hex(COL_DIM), 0);
        lv_label_set_text(s_days[i], wd[i]);
        lv_obj_set_pos(s_days[i], strip_start + i * cell_w + 4, strip_y);
    }

    // ── indicator row: ALM left, AM/PM right ──
    s_alm = mk_label("ALM",  &dseg14_modern_20, COL_DIM, LV_ALIGN_TOP_LEFT,  80, 105);
    s_am  = mk_label("AM",   &dseg14_modern_20, COL_DIM, LV_ALIGN_TOP_RIGHT, -110, 105);
    s_pm  = mk_label("PM",   &dseg14_modern_20, COL_DIM, LV_ALIGN_TOP_RIGHT,  -70, 105);

    // ── hero time (ghost backdrop + live) ──
    s_time_ghost = mk_label("88:88", &dseg7_modern_100, COL_GHOST,
                            LV_ALIGN_CENTER, 0, -20);
    s_time_live  = mk_label("00:00", &dseg7_modern_100, COL_LIT,
                            LV_ALIGN_CENTER, 0, -20);

    // ── sub readouts: seconds (left of center) + date (right of center) ──
    s_sec_ghost = mk_label("88", &dseg7_modern_36, COL_GHOST,
                           LV_ALIGN_CENTER, -90, 65);
    s_sec_live  = mk_label("00", &dseg7_modern_36, COL_LIT,
                           LV_ALIGN_CENTER, -90, 65);
    s_date      = mk_label("MAY 29", &dseg14_modern_28, COL_DIM,
                           LV_ALIGN_CENTER, 55, 65);

    // ── faint divider line between Stanley zone and Dad zone ──
    static lv_point_precise_t pts[] = {{60, 0}, {408, 0}};
    s_div_line = lv_line_create(s_scr);
    lv_line_set_points(s_div_line, pts, 2);
    lv_obj_set_style_line_color(s_div_line, lv_color_hex(COL_GHOST), 0);
    lv_obj_set_style_line_width(s_div_line, 1, 0);
    lv_obj_align(s_div_line, LV_ALIGN_CENTER, 0, 110);

    // ── dad row: "DAD PEK" + dual-time digits + state ──
    s_dad_lbl   = mk_label("DAD PEK", &dseg14_modern_20, COL_DIM,
                           LV_ALIGN_CENTER, -90, 140);
    s_dad_time  = mk_label("00:00",   &dseg7_modern_36,  COL_DIM,
                           LV_ALIGN_CENTER, 50, 140);
    s_dad_state = mk_label("AWAKE",   &dseg14_modern_20, COL_LIT,
                           LV_ALIGN_CENTER, 0, 175);

    // ── bottom bezel: brand-line ──
    s_brand_bot = mk_label("DUAL TIME WORLD", &dseg14_modern_20, COL_RIM,
                           LV_ALIGN_BOTTOM_MID, 0, -32);
}

static const char* monNameEN(int mon) {
    static const char* m[] = {"JAN","FEB","MAR","APR","MAY","JUN",
                              "JUL","AUG","SEP","OCT","NOV","DEC"};
    return m[mon % 12];
}

void update() {
    if (!s_time_live) return;
    time_t now = time(nullptr);

    struct tm son; tz_helper::localtime_in(TZ_SON, now, &son);

    // weekday: only current day is lit, others dim
    for (int i = 0; i < 7; i++) {
        bool active = (i == son.tm_wday);
        lv_obj_set_style_text_color(s_days[i],
            lv_color_hex(active ? COL_HIGHLITE : COL_DIM), 0);
    }

    // AM / PM
    bool is_am = son.tm_hour < 12;
    lv_obj_set_style_text_color(s_am, lv_color_hex(is_am  ? COL_HIGHLITE : COL_DIM), 0);
    lv_obj_set_style_text_color(s_pm, lv_color_hex(is_am  ? COL_DIM      : COL_HIGHLITE), 0);

    // hero time (24h, blinking colon)
    char buf[32];
    bool colon = (son.tm_sec & 1) == 0;
    snprintf(buf, sizeof(buf), "%02d%c%02d",
             son.tm_hour, colon ? ':' : ' ', son.tm_min);
    lv_label_set_text(s_time_live, buf);

    snprintf(buf, sizeof(buf), "%02d", son.tm_sec);
    lv_label_set_text(s_sec_live, buf);

    snprintf(buf, sizeof(buf), "%s %02d", monNameEN(son.tm_mon), son.tm_mday);
    lv_label_set_text(s_date, buf);

    // dad row
    struct tm dad; tz_helper::localtime_in(TZ_DAD, now, &dad);
    snprintf(buf, sizeof(buf), "%02d:%02d", dad.tm_hour, dad.tm_min);
    lv_label_set_text(s_dad_time, buf);

    dad_status::State st = dad_status::current(dad.tm_hour);
    lv_label_set_text(s_dad_state, dad_status::label(st));
    lv_obj_set_style_text_color(s_dad_state,
                                lv_color_hex(dad_status::color(st)), 0);

    // alert: expire and restore bezel-bottom branding when time's up
    if (s_alert_until && millis() >= s_alert_until) {
        s_alert_until = 0;
        lv_label_set_text(s_brand_bot, "DUAL TIME WORLD");
        lv_obj_set_style_text_color(s_brand_bot, lv_color_hex(COL_RIM), 0);
    }
}

void setStatus(bool wifi_up, bool mqtt_up, int unread_count) {
    if (s_wifi_dot) {
        lv_obj_set_style_bg_color(s_wifi_dot,
            lv_color_hex(wifi_up ? COL_LIT : COL_GHOST), 0);
    }
    if (s_mqtt_dot) {
        lv_obj_set_style_bg_color(s_mqtt_dot,
            lv_color_hex(mqtt_up ? COL_LIT : COL_GHOST), 0);
    }
    if (s_unread_lbl) {
        if (unread_count > 0) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%d", unread_count);
            lv_label_set_text(s_unread_lbl, buf);
        } else {
            lv_label_set_text(s_unread_lbl, "");
        }
    }
}

void showAlert(const char* text, uint32_t color_hex, uint32_t duration_ms) {
    if (!s_brand_bot) return;
    strncpy(s_alert_text, text, sizeof(s_alert_text) - 1);
    s_alert_text[sizeof(s_alert_text) - 1] = '\0';
    s_alert_color = color_hex;
    s_alert_until = millis() + duration_ms;
    lv_label_set_text(s_brand_bot, s_alert_text);
    lv_obj_set_style_text_color(s_brand_bot, lv_color_hex(s_alert_color), 0);
}

void destroy() {
    if (s_scr) lv_obj_clean(s_scr);   // wipe widgets only; parent is owned by caller
    s_scr = nullptr;
    for (int i = 0; i < 7; i++) s_days[i] = nullptr;
    s_brand_top = s_alm = s_am = s_pm = nullptr;
    s_time_ghost = s_time_live = s_sec_ghost = s_sec_live = nullptr;
    s_date = s_div_line = s_dad_lbl = s_dad_time = s_brand_bot = nullptr;
    s_wifi_dot = s_mqtt_dot = s_unread_lbl = nullptr;
}

}  // namespace face_lcd
