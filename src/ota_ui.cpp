#include "ota_ui.h"
#include <lvgl.h>
#include <stdio.h>

namespace ota_ui {

static lv_obj_t* s_overlay = nullptr;
static lv_obj_t* s_bar     = nullptr;
static lv_obj_t* s_pct     = nullptr;
static int       s_last    = -1;          // last-rendered % (redraw throttle)

void begin() {
    if (s_overlay) return;
    s_last = -1;

    s_overlay = lv_obj_create(lv_layer_top());     // always above the active app
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(0x140E08), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(s_overlay);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xE6A050), 0);
    lv_label_set_text(title, "UPDATING");
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -70);

    s_bar = lv_bar_create(s_overlay);
    lv_obj_set_size(s_bar, 240, 14);
    lv_obj_align(s_bar, LV_ALIGN_CENTER, 0, 0);
    lv_bar_set_range(s_bar, 0, 100);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x3A2A16), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, 7, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(0xE6A050), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar, 7, LV_PART_INDICATOR);

    s_pct = lv_label_create(s_overlay);
    lv_obj_set_style_text_font(s_pct, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_pct, lv_color_hex(0xF5E8D0), 0);
    lv_label_set_text(s_pct, "0%");
    lv_obj_align(s_pct, LV_ALIGN_CENTER, 0, 34);

    lv_obj_t* warn = lv_label_create(s_overlay);
    lv_obj_set_style_text_font(warn, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(warn, lv_color_hex(0xC0603A), 0);   // warm red
    lv_label_set_text(warn, "DO NOT POWER OFF");
    lv_obj_align(warn, LV_ALIGN_CENTER, 0, 80);

    lv_refr_now(NULL);
}

void progress(int pct) {
    if (!s_overlay) return;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    if (pct == s_last) return;          // only redraw when the integer % moves
    s_last = pct;
    lv_bar_set_value(s_bar, pct, LV_ANIM_OFF);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    lv_label_set_text(s_pct, buf);
    lv_refr_now(NULL);
}

void dismiss() {
    if (s_overlay) {
        lv_obj_del(s_overlay);
        s_overlay = s_bar = s_pct = nullptr;
        lv_refr_now(NULL);
    }
}

}  // namespace ota_ui
