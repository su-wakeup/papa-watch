#include "app_stub.h"
#include <lvgl.h>

LV_FONT_DECLARE(phosphor_64);
LV_FONT_DECLARE(mont_light_24);

namespace app_stub {

static lv_obj_t* s_root = nullptr;

void enter(lv_obj_t* parent, const char* icon_utf8, const char* name) {
    s_root = parent;
    lv_obj_set_style_bg_color(s_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);

    lv_obj_t* icon = lv_label_create(s_root);
    lv_obj_set_style_text_font(icon, &phosphor_64, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xE6A050), 0);
    lv_label_set_text(icon, icon_utf8);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t* label = lv_label_create(s_root);
    lv_obj_set_style_text_font(label, &mont_light_24, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF5E8D0), 0);
    lv_label_set_text(label, name);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 40);

    lv_obj_t* hint = lv_label_create(s_root);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x806848), 0);
    lv_label_set_text(hint, "BtnA back");
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 80);
}

void leave() {
    if (s_root) {
        lv_obj_clean(s_root);
        s_root = nullptr;
    }
}

void tick() {}

}  // namespace app_stub
