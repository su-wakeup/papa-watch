#include "status_bar.h"
#include <lvgl.h>
#include <stdio.h>

namespace status_bar {

static lv_obj_t* s_wifi   = nullptr;
static lv_obj_t* s_mqtt   = nullptr;
static lv_obj_t* s_unread = nullptr;

static constexpr uint32_t COL_ON   = 0xCFC2A8;
static constexpr uint32_t COL_OFF  = 0x4A3828;
static constexpr uint32_t COL_BELL = 0xE6A050;

void create(lv_obj_t* parent) {
    s_wifi = lv_label_create(parent);
    lv_obj_set_style_text_font(s_wifi, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_wifi, LV_SYMBOL_WIFI);
    lv_obj_align(s_wifi, LV_ALIGN_TOP_MID, -22, 14);

    s_mqtt = lv_label_create(parent);
    lv_obj_set_style_text_font(s_mqtt, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_mqtt, LV_SYMBOL_LOOP);
    lv_obj_align(s_mqtt, LV_ALIGN_TOP_MID, 0, 14);

    s_unread = lv_label_create(parent);
    lv_obj_set_style_text_font(s_unread, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_unread, lv_color_hex(COL_BELL), 0);
    lv_label_set_text(s_unread, "");
    lv_obj_align(s_unread, LV_ALIGN_TOP_MID, 30, 14);
}

void destroy() {
    s_wifi = s_mqtt = s_unread = nullptr;     // parent owns the widgets
}

void update(bool wifi_connected, bool mqtt_connected, int unread_count) {
    if (!s_wifi) return;
    lv_obj_set_style_text_color(s_wifi,
        lv_color_hex(wifi_connected ? COL_ON : COL_OFF), 0);
    lv_obj_set_style_text_color(s_mqtt,
        lv_color_hex(mqtt_connected ? COL_ON : COL_OFF), 0);
    if (unread_count > 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%s %d", LV_SYMBOL_BELL, unread_count);
        lv_label_set_text(s_unread, buf);
    } else {
        lv_label_set_text(s_unread, "");
    }
}

}  // namespace status_bar
