#include "face_alarm.h"
#include "alarm.h"
#include <lvgl.h>
#include <Arduino.h>
#include <stdio.h>

LV_FONT_DECLARE(mont_light_14);
LV_FONT_DECLARE(mont_light_20);
LV_FONT_DECLARE(mont_light_24);
LV_FONT_DECLARE(mont_light_32);

namespace face_alarm {

// Mode picker is a single row of three mutually-exclusive pill buttons:
//   OFF (alarm disabled) | DAILY (repeat every day) | ONCE (one-shot)
// Replaces the earlier separate ON/OFF switch + DAILY/ONCE pair + preview
// row — the user found that 5-level vertical stack cramped and fiddly on
// the round 466 panel.
static lv_obj_t* s_root        = nullptr;
static lv_obj_t* s_roller_h    = nullptr;
static lv_obj_t* s_roller_m    = nullptr;
static lv_obj_t* s_test_btn    = nullptr;
static lv_obj_t* s_btn_off     = nullptr;
static lv_obj_t* s_btn_daily   = nullptr;
static lv_obj_t* s_btn_once    = nullptr;

static char s_minute_opts[60 * 3 + 1];   // "00\n01\n...\n59\0"
static char s_hour_opts  [24 * 3 + 1];   // "00\n01\n...\n23\0"

static void build_opts() {
    char* p = s_hour_opts;
    for (int i = 0; i < 24; i++) p += sprintf(p, i ? "\n%02d" : "%02d", i);
    p = s_minute_opts;
    for (int i = 0; i < 60; i++) p += sprintf(p, i ? "\n%02d" : "%02d", i);
}

static void style_mode_btn(lv_obj_t* btn, bool selected) {
    lv_obj_set_style_bg_color(btn,
        lv_color_hex(selected ? 0xE6A050 : 0x2A1B0E), 0);
    lv_obj_set_style_border_color(btn,
        lv_color_hex(selected ? 0xE6A050 : 0x6A4A2C), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 18, 0);
    lv_obj_t* lbl = lv_obj_get_child(btn, 0);
    if (lbl) {
        lv_obj_set_style_text_color(lbl,
            lv_color_hex(selected ? 0x100A05 : 0xCFC2A8), 0);
    }
}

static void refresh_mode_buttons() {
    if (!s_btn_off) return;
    alarms::Config& c = alarms::get();
    bool off   = !c.enabled;
    bool daily =  c.enabled && c.mode == alarms::REPEAT_DAILY;
    bool once  =  c.enabled && c.mode == alarms::REPEAT_ONCE;
    style_mode_btn(s_btn_off,   off);
    style_mode_btn(s_btn_daily, daily);
    style_mode_btn(s_btn_once,  once);
}

static void style_roller(lv_obj_t* r) {
    lv_obj_set_style_text_font(r, &mont_light_32, LV_PART_MAIN);
    lv_obj_set_style_text_font(r, &mont_light_32, LV_PART_SELECTED);
    lv_obj_set_style_bg_color (r, lv_color_hex(0x1F1308), LV_PART_MAIN);
    lv_obj_set_style_bg_opa   (r, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(r, lv_color_hex(0x9C8870), LV_PART_MAIN);
    lv_obj_set_style_bg_color (r, lv_color_hex(0xE6A050), LV_PART_SELECTED);
    lv_obj_set_style_text_color(r, lv_color_hex(0x100A05), LV_PART_SELECTED);
    lv_obj_set_style_radius   (r, 12, LV_PART_MAIN);
    lv_obj_set_style_border_width(r, 0, LV_PART_MAIN);
}

static void on_roller_change(lv_event_t* e) {
    lv_obj_t* r = (lv_obj_t*)lv_event_get_target(e);
    uint16_t v = lv_roller_get_selected(r);
    alarms::Config& c = alarms::get();
    if (r == s_roller_h) c.hour   = (uint8_t)v;
    else                 c.minute = (uint8_t)v;
    alarms::save();
}

static void on_off_clicked(lv_event_t*) {
    alarms::get().enabled = false;
    alarms::save();
    refresh_mode_buttons();
}

static void on_daily_clicked(lv_event_t*) {
    alarms::get().enabled = true;
    alarms::get().mode    = alarms::REPEAT_DAILY;
    alarms::save();
    refresh_mode_buttons();
}

static void on_once_clicked(lv_event_t*) {
    alarms::get().enabled = true;
    alarms::get().mode    = alarms::REPEAT_ONCE;
    alarms::save();
    refresh_mode_buttons();
}

static void on_test_clicked(lv_event_t*) {
    alarms::fire();
}

static lv_obj_t* make_mode_btn(lv_obj_t* parent, const char* text,
                               int x_off, int y_off, lv_event_cb_t cb) {
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_size(btn, 105, 48);
    lv_obj_align(btn, LV_ALIGN_CENTER, x_off, y_off);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &mont_light_20, 0);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    return btn;
}

void create(lv_obj_t* parent) {
    s_root = parent;
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0x100A05), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    build_opts();

    // Vertical budget: round 466 panel → safe viewport is roughly ±215 from
    // centre before things start being clipped by the bezel. Pack tightly:
    //   title       y = -200
    //   rollers     centred at y = -90 (90px tall)
    //   switch      y = +20
    //   mode pills  y = +75
    //   preview     y = +120
    //   TEST btn    y = +175  (52px tall → bottom edge at +201)
    lv_obj_t* title = lv_label_create(s_root);
    lv_obj_set_style_text_font(title, &mont_light_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xE6A050), 0);
    lv_label_set_text(title, "ALARM");
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -200);

    alarms::Config& c = alarms::get();

    s_roller_h = lv_roller_create(s_root);
    lv_roller_set_options(s_roller_h, s_hour_opts, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(s_roller_h, 3);
    style_roller(s_roller_h);
    lv_obj_set_width(s_roller_h, 96);
    lv_obj_align(s_roller_h, LV_ALIGN_CENTER, -60, -90);
    lv_roller_set_selected(s_roller_h, c.hour, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_roller_h, on_roller_change, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t* colon = lv_label_create(s_root);
    lv_obj_set_style_text_font(colon, &mont_light_32, 0);
    lv_obj_set_style_text_color(colon, lv_color_hex(0xCFC2A8), 0);
    lv_label_set_text(colon, ":");
    lv_obj_align(colon, LV_ALIGN_CENTER, 0, -95);

    s_roller_m = lv_roller_create(s_root);
    lv_roller_set_options(s_roller_m, s_minute_opts, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(s_roller_m, 3);
    style_roller(s_roller_m);
    lv_obj_set_width(s_roller_m, 96);
    lv_obj_align(s_roller_m, LV_ALIGN_CENTER, 60, -90);
    lv_roller_set_selected(s_roller_m, c.minute, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_roller_m, on_roller_change, LV_EVENT_VALUE_CHANGED, nullptr);

    // One row, three mutually-exclusive states. Plenty of vertical air
    // above (rollers) and below (TEST) so a thumb can't miss.
    s_btn_off   = make_mode_btn(s_root, "OFF",   -112, 30, on_off_clicked);
    s_btn_daily = make_mode_btn(s_root, "DAILY",    0, 30, on_daily_clicked);
    s_btn_once  = make_mode_btn(s_root, "ONCE",  +112, 30, on_once_clicked);
    refresh_mode_buttons();

    s_test_btn = lv_button_create(s_root);
    lv_obj_set_size(s_test_btn, 200, 56);
    lv_obj_align(s_test_btn, LV_ALIGN_CENTER, 0, 130);
    lv_obj_set_style_bg_color(s_test_btn, lv_color_hex(0x2A1B0E), 0);
    lv_obj_set_style_border_color(s_test_btn, lv_color_hex(0x6A4A2C), 0);
    lv_obj_set_style_border_width(s_test_btn, 1, 0);
    lv_obj_set_style_radius(s_test_btn, 22, 0);
    lv_obj_add_event_cb(s_test_btn, on_test_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* tlbl = lv_label_create(s_test_btn);
    lv_obj_set_style_text_font(tlbl, &mont_light_20, 0);
    lv_obj_set_style_text_color(tlbl, lv_color_hex(0xE6A050), 0);
    lv_label_set_text(tlbl, "TEST");
    lv_obj_center(tlbl);

}

void update() {}

void destroy() {
    s_root = nullptr;
    s_roller_h = s_roller_m = s_test_btn = nullptr;
    s_btn_off = s_btn_daily = s_btn_once = nullptr;
}

}  // namespace face_alarm
