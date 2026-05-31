// face_lvgl — Phase 1 watch face. See header for scope.

#include "face_lvgl.h"
#include "tz_helper.h"
#include <lvgl.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

namespace face_lvgl {

static constexpr const char* TZ_SON = "PST8PDT,M3.2.0,M11.1.0";
static constexpr const char* TZ_DAD = "CST-8";

static lv_obj_t* s_date_lbl = nullptr;   // top: Stanley's date
static lv_obj_t* s_time_lbl = nullptr;   // hero: Stanley's time HH:MM
static lv_obj_t* s_sec_lbl  = nullptr;   // alive signal: SS
static lv_obj_t* s_dad_lbl  = nullptr;   // bottom: dad's time

static const char* dayNameEN(int wday) {
    static const char* d[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    return d[wday & 7];
}
static const char* monNameEN(int mon) {
    static const char* m[] = {"Jan","Feb","Mar","Apr","May","Jun",
                              "Jul","Aug","Sep","Oct","Nov","Dec"};
    return m[mon % 12];
}

void create() {
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    s_date_lbl = lv_label_create(scr);
    lv_obj_set_style_text_font(s_date_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_date_lbl, lv_color_hex(0xC0C0C0), 0);
    lv_obj_align(s_date_lbl, LV_ALIGN_TOP_MID, 0, 70);
    lv_label_set_text(s_date_lbl, "");

    s_time_lbl = lv_label_create(scr);
    lv_obj_set_style_text_font(s_time_lbl, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_time_lbl, lv_color_white(), 0);
    lv_obj_align(s_time_lbl, LV_ALIGN_CENTER, 0, -20);
    lv_label_set_text(s_time_lbl, "--:--");

    s_sec_lbl = lv_label_create(scr);
    lv_obj_set_style_text_font(s_sec_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_sec_lbl, lv_color_hex(0x808080), 0);
    lv_obj_align(s_sec_lbl, LV_ALIGN_CENTER, 0, 32);
    lv_label_set_text(s_sec_lbl, ":--");

    s_dad_lbl = lv_label_create(scr);
    lv_obj_set_style_text_font(s_dad_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_dad_lbl, lv_color_hex(0xA0A0A0), 0);
    lv_obj_align(s_dad_lbl, LV_ALIGN_BOTTOM_MID, 0, -80);
    lv_label_set_text(s_dad_lbl, "Dad --:-- PEK");
}

void update() {
    if (!s_time_lbl) return;
    time_t now = time(nullptr);

    struct tm son; tz_helper::localtime_in(TZ_SON, now, &son);

    char buf[32];
    snprintf(buf, sizeof(buf), "%s . %s %d",
             dayNameEN(son.tm_wday), monNameEN(son.tm_mon), son.tm_mday);
    lv_label_set_text(s_date_lbl, buf);

    // alive signal: blink colon each second
    bool show_colon = (son.tm_sec & 1) == 0;
    snprintf(buf, sizeof(buf), "%02d%c%02d", son.tm_hour,
             show_colon ? ':' : ' ', son.tm_min);
    lv_label_set_text(s_time_lbl, buf);

    snprintf(buf, sizeof(buf), ":%02d", son.tm_sec);
    lv_label_set_text(s_sec_lbl, buf);

    struct tm dad; tz_helper::localtime_in(TZ_DAD, now, &dad);
    snprintf(buf, sizeof(buf), "Dad %02d:%02d PEK", dad.tm_hour, dad.tm_min);
    lv_label_set_text(s_dad_lbl, buf);
}

}  // namespace face_lvgl
