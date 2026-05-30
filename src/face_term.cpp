#include "face_term.h"
#include "dad_status.h"
#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

LV_FONT_DECLARE(orbitron_64);
LV_FONT_DECLARE(orbitron_22);

namespace face_term {

static constexpr const char* TZ_SON = "PST8PDT,M3.2.0,M11.1.0";
static constexpr const char* TZ_DAD = "CST-8";

// neon palette
static constexpr uint32_t COL_BG       = 0x000000;
static constexpr uint32_t COL_CYAN     = 0x00E5FF;     // primary
static constexpr uint32_t COL_MAGENTA  = 0xFF1493;     // accent
static constexpr uint32_t COL_OK       = 0x39FF14;     // ok/online
static constexpr uint32_t COL_AMBER    = 0xFFB000;     // warning
static constexpr uint32_t COL_DIM      = 0x365A6A;     // dim cyan for labels

static lv_obj_t* s_root          = nullptr;
static lv_obj_t* s_header        = nullptr;
static lv_obj_t* s_pulse         = nullptr;
static lv_obj_t* s_time_hero     = nullptr;
static lv_obj_t* s_subtitle      = nullptr;
static lv_obj_t* s_dad_lbl       = nullptr;
static lv_obj_t* s_dad_time      = nullptr;
static lv_obj_t* s_dad_state     = nullptr;
static lv_obj_t* s_unread_lbl    = nullptr;
static lv_obj_t* s_unread_count  = nullptr;
static lv_obj_t* s_ticker        = nullptr;

static int s_unread = 0;
static lv_anim_t s_pulse_anim;

// ── pulse-dot opacity animator ──
static void pulse_set_opa(void* obj, int32_t v) {
    lv_obj_set_style_bg_opa((lv_obj_t*)obj, (lv_opa_t)v, 0);
}

// ── corner bracket: two thin bars forming an L at the given anchor ──
static void mk_corner(lv_obj_t* parent, lv_align_t a, int xo, int yo, bool right_side, bool bottom_side, uint32_t color) {
    constexpr int LEN = 22;
    constexpr int W   = 2;

    lv_obj_t* h = lv_obj_create(parent);
    lv_obj_remove_style_all(h);
    lv_obj_set_size(h, LEN, W);
    lv_obj_set_style_bg_color(h, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);
    lv_obj_align(h, a, xo, yo);

    lv_obj_t* v = lv_obj_create(parent);
    lv_obj_remove_style_all(v);
    lv_obj_set_size(v, W, LEN);
    lv_obj_set_style_bg_color(v, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(v, LV_OPA_COVER, 0);
    int vx = xo + (right_side ? LEN - W : 0);
    int vy = yo + (bottom_side ? -LEN + W : 0);
    lv_obj_align(v, a, vx, vy);
}

void create(lv_obj_t* parent) {
    s_root = parent;
    lv_obj_set_style_bg_color(s_root, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);

    // ── corner brackets ──
    mk_corner(s_root, LV_ALIGN_TOP_LEFT,      80,  85, false, false, COL_CYAN);
    mk_corner(s_root, LV_ALIGN_TOP_RIGHT,    -80,  85, true,  false, COL_CYAN);
    mk_corner(s_root, LV_ALIGN_BOTTOM_LEFT,   80, -85, false, true,  COL_CYAN);
    mk_corner(s_root, LV_ALIGN_BOTTOM_RIGHT, -80, -85, true,  true,  COL_CYAN);

    // ── header: STANLEY//SYS + pulsing magenta dot ──
    s_header = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_header, &orbitron_22, 0);
    lv_obj_set_style_text_color(s_header, lv_color_hex(COL_MAGENTA), 0);
    lv_label_set_text(s_header, "STANLEY//SYS");
    lv_obj_align(s_header, LV_ALIGN_TOP_MID, -28, 108);

    s_pulse = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_pulse);
    lv_obj_set_size(s_pulse, 12, 12);
    lv_obj_set_style_radius(s_pulse, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_pulse, lv_color_hex(COL_MAGENTA), 0);
    lv_obj_set_style_bg_opa(s_pulse, LV_OPA_COVER, 0);
    lv_obj_align(s_pulse, LV_ALIGN_TOP_MID, 90, 113);

    lv_anim_init(&s_pulse_anim);
    lv_anim_set_var(&s_pulse_anim, s_pulse);
    lv_anim_set_values(&s_pulse_anim, 80, 255);
    lv_anim_set_time(&s_pulse_anim, 900);
    lv_anim_set_playback_time(&s_pulse_anim, 900);
    lv_anim_set_repeat_count(&s_pulse_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&s_pulse_anim, pulse_set_opa);
    lv_anim_start(&s_pulse_anim);

    // ── BIG cyan hero time ──
    s_time_hero = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_time_hero, &orbitron_64, 0);
    lv_obj_set_style_text_color(s_time_hero, lv_color_hex(COL_CYAN), 0);
    lv_label_set_text(s_time_hero, "00:00");
    lv_obj_align(s_time_hero, LV_ALIGN_CENTER, 0, -38);

    s_subtitle = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_subtitle, &orbitron_22, 0);
    lv_obj_set_style_text_color(s_subtitle, lv_color_hex(COL_MAGENTA), 0);
    lv_label_set_text(s_subtitle, "PT.LA.US");
    lv_obj_align(s_subtitle, LV_ALIGN_CENTER, 0, 12);

    // ── DAD panel (bottom-left) ──
    s_dad_lbl = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_dad_lbl, &orbitron_22, 0);
    lv_obj_set_style_text_color(s_dad_lbl, lv_color_hex(COL_DIM), 0);
    lv_label_set_text(s_dad_lbl, "DAD");
    lv_obj_align(s_dad_lbl, LV_ALIGN_CENTER, -90, 60);

    s_dad_time = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_dad_time, &orbitron_22, 0);
    lv_obj_set_style_text_color(s_dad_time, lv_color_hex(COL_CYAN), 0);
    lv_label_set_text(s_dad_time, "00:00");
    lv_obj_align(s_dad_time, LV_ALIGN_CENTER, -90, 87);

    s_dad_state = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_dad_state, &orbitron_22, 0);
    lv_obj_set_style_text_color(s_dad_state, lv_color_hex(COL_OK), 0);
    lv_label_set_text(s_dad_state, "ONLINE");
    lv_obj_align(s_dad_state, LV_ALIGN_CENTER, -90, 113);

    // ── UNREAD panel (bottom-right) ──
    s_unread_lbl = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_unread_lbl, &orbitron_22, 0);
    lv_obj_set_style_text_color(s_unread_lbl, lv_color_hex(COL_DIM), 0);
    lv_label_set_text(s_unread_lbl, "UNREAD");
    lv_obj_align(s_unread_lbl, LV_ALIGN_CENTER, 90, 60);

    s_unread_count = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_unread_count, &orbitron_64, 0);
    lv_obj_set_style_text_color(s_unread_count, lv_color_hex(COL_MAGENTA), 0);
    lv_label_set_text(s_unread_count, "0");
    lv_obj_align(s_unread_count, LV_ALIGN_CENTER, 90, 100);
}

void setUnread(int n) { s_unread = n; }

void update() {
    if (!s_root) return;
    time_t now = time(nullptr);

    setenv("TZ", TZ_SON, 1); tzset();
    struct tm son; localtime_r(&now, &son);

    setenv("TZ", TZ_DAD, 1); tzset();
    struct tm dad; localtime_r(&now, &dad);

    char buf[24];

    // hero time
    snprintf(buf, sizeof(buf), "%02d:%02d", son.tm_hour, son.tm_min);
    lv_label_set_text(s_time_hero, buf);

    // dad time
    snprintf(buf, sizeof(buf), "%02d:%02d", dad.tm_hour, dad.tm_min);
    lv_label_set_text(s_dad_time, buf);

    // dad state via shared dad_status (auto by Beijing hour + Telegram override)
    dad_status::State st = dad_status::current(dad.tm_hour);
    lv_label_set_text(s_dad_state, dad_status::label(st));
    lv_obj_set_style_text_color(s_dad_state,
                                lv_color_hex(dad_status::color(st)), 0);

    // unread count
    snprintf(buf, sizeof(buf), "%d", s_unread);
    lv_label_set_text(s_unread_count, buf);
}

void destroy() {
    s_root = nullptr;
    s_header = s_pulse = s_time_hero = s_subtitle = nullptr;
    s_dad_lbl = s_dad_time = s_dad_state = nullptr;
    s_unread_lbl = s_unread_count = s_ticker = nullptr;
}

}  // namespace face_term
