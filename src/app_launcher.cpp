#include "app_launcher.h"
#include "app.h"
#include "status_bar.h"
#include <lvgl.h>

LV_FONT_DECLARE(phosphor_64);
LV_FONT_DECLARE(phosphor_128);
LV_FONT_DECLARE(mont_light_14);
LV_FONT_DECLARE(mont_light_32);

namespace app_launcher {

// We exclude g_apps[0] (the launcher's own slot) from the wheel — only the
// 7 real apps cycle. Index here is into [1..g_apps_count-1] inclusive.
static int  s_sel    = 1;
static lv_obj_t* s_root         = nullptr;
static lv_obj_t* s_icon_left    = nullptr;
static lv_obj_t* s_icon_center  = nullptr;
static lv_obj_t* s_icon_right   = nullptr;
static lv_obj_t* s_name         = nullptr;
static lv_obj_t* s_hint_a       = nullptr;
static lv_obj_t* s_hint_b       = nullptr;

static int wheel_count() { return g_apps_count - 1; }   // skip launcher slot

// Wrap idx (1..g_apps_count-1) through the seven real-app slots.
static const App* app_at_offset(int delta) {
    int n   = wheel_count();
    int rel = ((s_sel - 1) + delta) % n;
    if (rel < 0) rel += n;
    return g_apps[1 + rel];
}

static void on_center_click(lv_event_t*) {
    app_runtime::switch_to(app_at_offset(0));
}

static void render() {
    if (!s_icon_center) return;
    lv_label_set_text(s_icon_left,   app_at_offset(-1)->icon_utf8);
    lv_label_set_text(s_icon_center, app_at_offset( 0)->icon_utf8);
    lv_label_set_text(s_icon_right,  app_at_offset(+1)->icon_utf8);
    lv_label_set_text(s_name,        app_at_offset( 0)->name);
}

void enter(lv_obj_t* parent) {
    s_root = parent;
    lv_obj_set_style_bg_color(s_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);

    // Left/right "neighbour" icons — smaller, muted; hint at what's queued
    // up next without competing with the focal icon.
    s_icon_left = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_icon_left, &phosphor_64, 0);
    lv_obj_set_style_text_color(s_icon_left, lv_color_hex(0x705840), 0);
    lv_obj_align(s_icon_left, LV_ALIGN_CENTER, -160, -10);

    s_icon_right = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_icon_right, &phosphor_64, 0);
    lv_obj_set_style_text_color(s_icon_right, lv_color_hex(0x705840), 0);
    lv_obj_align(s_icon_right, LV_ALIGN_CENTER, 160, -10);

    // Center focal icon — big, warm amber, tap-to-enter
    s_icon_center = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_icon_center, &phosphor_128, 0);
    lv_obj_set_style_text_color(s_icon_center, lv_color_hex(0xE6A050), 0);
    lv_obj_align(s_icon_center, LV_ALIGN_CENTER, 0, -20);
    lv_obj_add_flag(s_icon_center, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_icon_center, on_center_click, LV_EVENT_CLICKED, nullptr);

    s_name = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_name, &mont_light_32, 0);
    lv_obj_set_style_text_color(s_name, lv_color_hex(0xF5E8D0), 0);
    lv_obj_align(s_name, LV_ALIGN_CENTER, 0, 90);

    // BtnA / BtnB hints near the physical button locations on the case.
    s_hint_a = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_hint_a, &mont_light_14, 0);
    lv_obj_set_style_text_color(s_hint_a, lv_color_hex(0x705840), 0);
    lv_label_set_text(s_hint_a, "< A");
    lv_obj_align(s_hint_a, LV_ALIGN_BOTTOM_LEFT, 36, -22);

    s_hint_b = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_hint_b, &mont_light_14, 0);
    lv_obj_set_style_text_color(s_hint_b, lv_color_hex(0x705840), 0);
    lv_label_set_text(s_hint_b, "B >");
    lv_obj_align(s_hint_b, LV_ALIGN_BOTTOM_RIGHT, -36, -22);

    // System status only lives on the home screen — watch faces have their
    // own face-specific indicators; other apps are intentionally clean.
    status_bar::create(s_root);

    render();
}

void leave() {
    if (s_root) {
        lv_obj_clean(s_root);
        s_root = nullptr;
    }
    s_icon_left = s_icon_center = s_icon_right = s_name = nullptr;
    s_hint_a = s_hint_b = nullptr;
    status_bar::destroy();
}

void tick() {}

void rotate_left() {
    int n = wheel_count();
    s_sel = ((s_sel - 1 - 1 + n) % n) + 1;
    render();
}

void rotate_right() {
    int n = wheel_count();
    s_sel = ((s_sel - 1 + 1) % n) + 1;
    render();
}

void activate_center() {
    app_runtime::switch_to(app_at_offset(0));
}

}  // namespace app_launcher
