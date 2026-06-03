#include "status_bar.h"
#include <lvgl.h>
#include <stdio.h>

namespace status_bar {

static lv_obj_t* s_wifi   = nullptr;
static lv_obj_t* s_mqtt   = nullptr;
static lv_obj_t* s_unread = nullptr;
static lv_obj_t* s_batt   = nullptr;

// Dim, warm palette. The panel's black is true-black (pixels off), so even a
// muted colour pops — keep these low-luminance so the status recedes.
static constexpr uint32_t COL_ON   = 0x4C4029;   // connected: very dim gold
static constexpr uint32_t COL_OFF  = 0x241B10;   // disconnected: nearly black
static constexpr uint32_t COL_BELL = 0x6E4E26;
static constexpr uint32_t COL_BATT = 0x453E2C;

// Items sit on a gentle top arc (centre highest, sides lower) so they hug the
// round edge instead of a flat row. (x, y) are TOP_MID align offsets.
void create(lv_obj_t* parent) {
    s_batt = lv_label_create(parent);   // apex — the info people actually want
    lv_obj_set_style_text_font(s_batt, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_batt, lv_color_hex(COL_BATT), 0);
    lv_label_set_text(s_batt, "");
    lv_obj_align(s_batt, LV_ALIGN_TOP_MID, 0, 16);

    s_wifi = lv_label_create(parent);
    lv_obj_set_style_text_font(s_wifi, &lv_font_montserrat_18, 0);
    lv_label_set_text(s_wifi, LV_SYMBOL_WIFI);
    lv_obj_align(s_wifi, LV_ALIGN_TOP_MID, -64, 30);

    s_mqtt = lv_label_create(parent);
    lv_obj_set_style_text_font(s_mqtt, &lv_font_montserrat_18, 0);
    lv_label_set_text(s_mqtt, LV_SYMBOL_LOOP);
    lv_obj_align(s_mqtt, LV_ALIGN_TOP_MID, 64, 30);

    s_unread = lv_label_create(parent);   // transient — sits just under the apex
    lv_obj_set_style_text_font(s_unread, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_unread, lv_color_hex(COL_BELL), 0);
    lv_label_set_text(s_unread, "");
    lv_obj_align(s_unread, LV_ALIGN_TOP_MID, 0, 42);
}

void destroy() {
    s_wifi = s_mqtt = s_unread = s_batt = nullptr;  // parent owns the widgets
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

void setBattery(int pct, bool charging) {
    if (!s_batt) return;
    char buf[16];
    snprintf(buf, sizeof(buf), "%s%d%%", charging ? LV_SYMBOL_CHARGE " " : "", pct);
    lv_label_set_text(s_batt, buf);
}

int battPctFromMv(int mv) {
    // Approximate single-cell Li-ion resting OCV→SoC. M5Unified's getBatteryLevel
    // is a flat linear 3300→4100mV map that pins 100% for everything above 4.10V;
    // this curve lets the top of the range actually move. Still approximate
    // (voltage sags under load, recovers at rest) but far more honest mid-range.
    static const struct { int mv; int pct; } TBL[] = {
        {4200,100},{4100,90},{4000,76},{3900,60},{3800,42},
        {3700,24},{3600,11},{3500,5},{3300,0},
    };
    const int N = sizeof(TBL) / sizeof(TBL[0]);
    if (mv >= TBL[0].mv)   return 100;
    if (mv <= TBL[N-1].mv) return 0;
    for (int i = 0; i < N - 1; i++) {
        if (mv <= TBL[i].mv && mv > TBL[i+1].mv) {
            int dmv  = TBL[i].mv  - TBL[i+1].mv;
            int dpct = TBL[i].pct - TBL[i+1].pct;
            return TBL[i+1].pct + (mv - TBL[i+1].mv) * dpct / dmv;
        }
    }
    return 0;
}

}  // namespace status_bar
