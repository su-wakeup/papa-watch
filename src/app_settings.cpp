#include "app_settings.h"
#include "app.h"
#include "ota.h"
#include "wifi_setup.h"
#include "dad_status.h"
#include "geo.h"
#include <lvgl.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <WiFi.h>
#include <Arduino.h>

LV_FONT_DECLARE(mont_light_14);
LV_FONT_DECLARE(mont_light_20);
LV_FONT_DECLARE(mont_light_24);

extern const char* OTA_MANIFEST_URL;     // defined in main.cpp
// FIRMWARE_VERSION lives in the ota:: namespace (see ota.h)

namespace app_settings {

// ── persisted preferences ────────────────────────────────────────────────
static uint8_t s_brightness    = 200;
static uint8_t s_volume        = 180;
static bool    s_vibration_on  = true;
static bool    s_skin_rich     = true;   // launcher icons: rich colour vs simple

bool vibrationEnabled() { return s_vibration_on; }
uint8_t brightness()    { return s_brightness; }

static void load_prefs() {
    Preferences p;
    p.begin("papa-watch", true);     // read-only
    s_brightness   = p.getUChar("brightness", 200);
    s_volume       = p.getUChar("volume",     180);
    s_vibration_on = p.getBool ("vibrate",    true);
    s_skin_rich    = p.getBool ("skin_rich",  true);
    p.end();
}

static void save_uchar(const char* key, uint8_t v) {
    Preferences p; p.begin("papa-watch", false); p.putUChar(key, v); p.end();
}
static void save_bool(const char* key, bool v) {
    Preferences p; p.begin("papa-watch", false); p.putBool (key, v); p.end();
}

void apply_on_boot() {
    load_prefs();
    M5.Display.setBrightness(s_brightness);
    M5.Speaker.setVolume(s_volume);
}

// ── widget refs ──────────────────────────────────────────────────────────
static lv_obj_t* s_root      = nullptr;
static lv_obj_t* s_scroll    = nullptr;
static lv_obj_t* s_wifi_row  = nullptr;
static lv_obj_t* s_dad_row   = nullptr;
static lv_obj_t* s_geo_row   = nullptr;

// ── row factory ──────────────────────────────────────────────────────────
static lv_obj_t* make_row(lv_obj_t* parent) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 76);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left (row, 18, 0);
    lv_obj_set_style_pad_right(row, 18, 0);
    lv_obj_set_style_bg_color (row, lv_color_hex(0x1F1308), 0);
    lv_obj_set_style_bg_opa   (row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius   (row, 14, 0);
    lv_obj_set_style_margin_bottom(row, 8, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

static lv_obj_t* row_text(lv_obj_t* row, const char* txt, uint32_t color = 0xF5E8D0) {
    lv_obj_t* lbl = lv_label_create(row);
    lv_obj_set_style_text_font(lbl, &mont_light_24, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    lv_label_set_text(lbl, txt);
    lv_obj_set_flex_grow(lbl, 1);
    return lbl;
}

// ── inline-control event handlers ────────────────────────────────────────
static void on_bright_change(lv_event_t* e) {
    lv_obj_t* sl = (lv_obj_t*)lv_event_get_target(e);
    s_brightness = (uint8_t)lv_slider_get_value(sl);
    M5.Display.setBrightness(s_brightness);
    save_uchar("brightness", s_brightness);
}

static void on_vol_change(lv_event_t* e) {
    lv_obj_t* sl = (lv_obj_t*)lv_event_get_target(e);
    s_volume = (uint8_t)lv_slider_get_value(sl);
    M5.Speaker.setVolume(s_volume);
    save_uchar("volume", s_volume);
}

static void on_vib_change(lv_event_t* e) {
    lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
    s_vibration_on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    save_bool("vibrate", s_vibration_on);
}

static void on_skin_change(lv_event_t* e) {
    lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
    s_skin_rich = lv_obj_has_state(sw, LV_STATE_CHECKED);
    save_bool("skin_rich", s_skin_rich);
    // The launcher re-reads the skin in enter(), so it takes effect next time
    // the home screen is shown (e.g. on returning from Settings).
}

// ── action rows ──────────────────────────────────────────────────────────
static void on_wifi_click(lv_event_t*) {
    // The interactive flow draws to M5GFX (not LVGL) and blocks until the
    // user picks a SSID + enters password. After it returns, we bounce back
    // to the launcher so LVGL gets to redraw a known-good screen.
    wifi_setup::runInteractiveConfig();
    app_runtime::back_to_launcher();
}

static void on_ota_click(lv_event_t* e) {
    lv_obj_t* row = (lv_obj_t*)lv_event_get_current_target(e);
    lv_obj_t* lbl = lv_obj_get_child(row, 0);
    lv_label_set_text(lbl, "Checking...");
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xE6A050), 0);
    // Force the "Checking..." text onto the panel before checkAndUpdate
    // blocks for the manifest fetch (and possibly the firmware download).
    lv_refr_now(NULL);
    // force=false: only update when latest tag > FIRMWARE_VERSION. With
    // force=true the device re-downloads and re-flashes the same firmware
    // on every tap — locks the UI for 30-60s, then ESP.restart()s back to
    // the same version, making "Check Updates" look broken.
    bool started_update = ota::checkAndUpdate(OTA_MANIFEST_URL, false);
    // If checkAndUpdate kicked off a real update, it ESP.restart()s and
    // never returns — so reaching here always means "no update applied".
    lv_label_set_text(lbl, started_update ? "Updating..." : "No new release");
}

static void on_geo_click(lv_event_t*) {
    // Cycle: Auto → preset 1 → preset 2 → ... → Auto
    int next = (geo::getOverrideIdx() + 1) % (geo::PRESETS_N + 1);
    geo::setOverrideIdx(next);
    if (s_geo_row) {
        char buf[64];
        if (next == 0) snprintf(buf, sizeof(buf), "Location: Auto (IP)");
        else snprintf(buf, sizeof(buf), "Location: %s", geo::PRESETS[next - 1].name);
        lv_obj_t* lbl = lv_obj_get_child(s_geo_row, 0);
        lv_label_set_text(lbl, buf);
    }
}

static void on_dad_click(lv_event_t*) {
    // Cycle through the user-facing override states; "auto" clears the
    // override and lets dad_status fall back to the timezone-driven default.
    static const char* states[] = {"auto", "free", "busy", "away", "thinking"};
    static const int   N        = sizeof(states) / sizeof(states[0]);
    static int s_idx = 0;
    s_idx = (s_idx + 1) % N;
    if (s_idx == 0) {
        dad_status::clearOverride();
    } else {
        dad_status::setOverride(states[s_idx], 6);
    }
    if (s_dad_row) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Dad status: %s", states[s_idx]);
        lv_obj_t* lbl = lv_obj_get_child(s_dad_row, 0);
        lv_label_set_text(lbl, buf);
    }
}

void enter(lv_obj_t* parent) {
    s_root = parent;
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0x100A05), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);

    s_scroll = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_scroll);
    lv_obj_set_size(s_scroll, 360, 420);
    lv_obj_align(s_scroll, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_layout(s_scroll, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_scroll, 6, 0);
    lv_obj_set_scrollbar_mode(s_scroll, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(s_scroll, LV_DIR_VER);

    // Brightness slider
    {
        lv_obj_t* row = make_row(s_scroll);
        row_text(row, "Brightness");
        lv_obj_t* sl = lv_slider_create(row);
        lv_obj_set_size(sl, 150, 12);
        lv_slider_set_range(sl, 30, 255);
        lv_slider_set_value(sl, s_brightness, LV_ANIM_OFF);
        lv_obj_set_style_bg_color (sl, lv_color_hex(0x4A3828), LV_PART_MAIN);
        lv_obj_set_style_bg_color (sl, lv_color_hex(0xE6A050), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color (sl, lv_color_hex(0xF5E8D0), LV_PART_KNOB);
        lv_obj_add_event_cb(sl, on_bright_change, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Volume slider
    {
        lv_obj_t* row = make_row(s_scroll);
        row_text(row, "Volume");
        lv_obj_t* sl = lv_slider_create(row);
        lv_obj_set_size(sl, 150, 12);
        lv_slider_set_range(sl, 0, 255);
        lv_slider_set_value(sl, s_volume, LV_ANIM_OFF);
        lv_obj_set_style_bg_color (sl, lv_color_hex(0x4A3828), LV_PART_MAIN);
        lv_obj_set_style_bg_color (sl, lv_color_hex(0xE6A050), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color (sl, lv_color_hex(0xF5E8D0), LV_PART_KNOB);
        lv_obj_add_event_cb(sl, on_vol_change, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Vibration switch
    {
        lv_obj_t* row = make_row(s_scroll);
        row_text(row, "Vibrate");
        lv_obj_t* sw = lv_switch_create(row);
        if (s_vibration_on) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(sw, lv_color_hex(0x4A3828), LV_PART_MAIN);
        lv_obj_set_style_bg_color(sw, lv_color_hex(0xE6A050), LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, on_vib_change, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Icon skin switch — on = rich colour pack, off = simple Phosphor glyphs.
    {
        lv_obj_t* row = make_row(s_scroll);
        row_text(row, "Rich icons");
        lv_obj_t* sw = lv_switch_create(row);
        if (s_skin_rich) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(sw, lv_color_hex(0x4A3828), LV_PART_MAIN);
        lv_obj_set_style_bg_color(sw, lv_color_hex(0xE6A050), LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, on_skin_change, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // WiFi row — tap to reconfigure
    {
        s_wifi_row = make_row(s_scroll);
        char buf[64];
        snprintf(buf, sizeof(buf), "WiFi: %s",
            (WiFi.status() == WL_CONNECTED) ? WiFi.SSID().c_str() : "(none)");
        row_text(s_wifi_row, buf);
        lv_obj_t* arrow = lv_label_create(s_wifi_row);
        lv_obj_set_style_text_font(arrow, &mont_light_20, 0);
        lv_obj_set_style_text_color(arrow, lv_color_hex(0x705840), 0);
        lv_label_set_text(arrow, ">");
        lv_obj_add_flag(s_wifi_row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_wifi_row, on_wifi_click, LV_EVENT_CLICKED, nullptr);
    }

    // OTA check row
    {
        lv_obj_t* row = make_row(s_scroll);
        row_text(row, "Check Updates");
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, on_ota_click, LV_EVENT_CLICKED, nullptr);
    }

    // Location override row — cycles Auto → preset cities → Auto
    {
        s_geo_row = make_row(s_scroll);
        int ov = geo::getOverrideIdx();
        char buf[64];
        if (ov == 0) snprintf(buf, sizeof(buf), "Location: Auto (IP)");
        else snprintf(buf, sizeof(buf), "Location: %s", geo::PRESETS[ov - 1].name);
        row_text(s_geo_row, buf);
        lv_obj_add_flag(s_geo_row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_geo_row, on_geo_click, LV_EVENT_CLICKED, nullptr);
    }

    // Dad status override row
    {
        s_dad_row = make_row(s_scroll);
        char buf[64];
        snprintf(buf, sizeof(buf), "Dad status: %s",
            dad_status::overrideActive() ? "override" : "auto");
        row_text(s_dad_row, buf);
        lv_obj_add_flag(s_dad_row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_dad_row, on_dad_click, LV_EVENT_CLICKED, nullptr);
    }

    // About — version + uptime
    {
        lv_obj_t* row = make_row(s_scroll);
        char buf[64];
        uint32_t up_min = millis() / 60000;
        snprintf(buf, sizeof(buf), "v%s  up %u min", ota::FIRMWARE_VERSION, up_min);
        row_text(row, buf, 0xB88860);
    }
}

void leave() {
    if (s_root) { lv_obj_clean(s_root); s_root = nullptr; }
    s_scroll = nullptr;
    s_wifi_row = s_dad_row = s_geo_row = nullptr;
}

void tick() {}

void scroll_up()   { if (s_scroll) lv_obj_scroll_by(s_scroll, 0,  64, LV_ANIM_ON); }
void scroll_down() { if (s_scroll) lv_obj_scroll_by(s_scroll, 0, -64, LV_ANIM_ON); }

}  // namespace app_settings
