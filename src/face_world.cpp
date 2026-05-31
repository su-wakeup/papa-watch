#include "face_world.h"
#include <lvgl.h>
#include <Arduino.h>
#include <time.h>
#include <stdio.h>

LV_FONT_DECLARE(mont_light_14);
LV_FONT_DECLARE(mont_light_20);
LV_FONT_DECLARE(mont_light_24);

namespace face_world {

// Cities are listed in roughly west-to-east timezone order. PAPA's Beijing
// row is intentionally first and given the bigger font / amber tint — it's
// the answer Stanley wants most often.
struct CityRow {
    const char* name;
    const char* tz;     // POSIX TZ string, DST baked in
    bool        focal;  // true → row gets amber + larger font
};

static const CityRow CITIES[] = {
    { "Beijing",  "CST-8",                          true  },   // PAPA — focal
    { "Los Gatos","PST8PDT,M3.2.0,M11.1.0",         false },   // Stanley
    { "NYC",      "EST5EDT,M3.2.0,M11.1.0",         false },
    { "London",   "GMT0BST,M3.5.0/1,M10.5.0",       false },
    { "Tokyo",    "JST-9",                          false },
    { "Sydney",   "AEST-10AEDT,M10.1.0,M4.1.0/3",   false },
};
static constexpr int N = sizeof(CITIES) / sizeof(CITIES[0]);

static lv_obj_t* s_root            = nullptr;
static lv_obj_t* s_name_lbl[N]     = {};
static lv_obj_t* s_time_lbl[N]     = {};
static lv_obj_t* s_day_lbl [N]     = {};

// Each row uses three labels positioned in columns; total content height
// fits comfortably inside the round 466 panel's inscribed circle.
static constexpr int ROW_GAP   = 44;
static constexpr int Y_FIRST   = -110;

void create(lv_obj_t* parent) {
    s_root = parent;
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0x100A05), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    // Title strip
    lv_obj_t* title = lv_label_create(s_root);
    lv_obj_set_style_text_font(title, &mont_light_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x806848), 0);
    lv_label_set_text(title, "WORLD CLOCK");
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -190);

    for (int i = 0; i < N; i++) {
        int y = Y_FIRST + i * ROW_GAP;
        const lv_font_t* font = CITIES[i].focal ? &mont_light_24 : &mont_light_20;
        uint32_t color = CITIES[i].focal ? 0xE6A050 : 0xCFC2A8;

        s_name_lbl[i] = lv_label_create(s_root);
        lv_obj_set_style_text_font (s_name_lbl[i], font, 0);
        lv_obj_set_style_text_color(s_name_lbl[i], lv_color_hex(color), 0);
        lv_label_set_text(s_name_lbl[i], CITIES[i].name);
        lv_obj_align(s_name_lbl[i], LV_ALIGN_LEFT_MID, 64, y);

        s_time_lbl[i] = lv_label_create(s_root);
        lv_obj_set_style_text_font (s_time_lbl[i], font, 0);
        lv_obj_set_style_text_color(s_time_lbl[i], lv_color_hex(color), 0);
        lv_label_set_text(s_time_lbl[i], "--:--");
        lv_obj_align(s_time_lbl[i], LV_ALIGN_CENTER, 60, y);

        s_day_lbl[i] = lv_label_create(s_root);
        lv_obj_set_style_text_font (s_day_lbl[i], &mont_light_14, 0);
        lv_obj_set_style_text_color(s_day_lbl[i], lv_color_hex(0x806848), 0);
        lv_label_set_text(s_day_lbl[i], "---");
        lv_obj_align(s_day_lbl[i], LV_ALIGN_RIGHT_MID, -50, y);
    }

    update();
}

void update() {
    if (!s_root) return;
    time_t now = time(nullptr);

    static const char* DAYS[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
    char buf[8];

    for (int i = 0; i < N; i++) {
        setenv("TZ", CITIES[i].tz, 1); tzset();
        struct tm t; localtime_r(&now, &t);
        snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
        lv_label_set_text(s_time_lbl[i], buf);
        lv_label_set_text(s_day_lbl[i], DAYS[t.tm_wday & 7]);
    }
}

void destroy() {
    s_root = nullptr;
    for (int i = 0; i < N; i++) {
        s_name_lbl[i] = s_time_lbl[i] = s_day_lbl[i] = nullptr;
    }
}

}  // namespace face_world
