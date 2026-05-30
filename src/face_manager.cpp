#include "face_manager.h"
#include "face_lcd.h"
#include "face_mech.h"
#include "face_term.h"
#include <lvgl.h>
#include <Preferences.h>
#include <Arduino.h>

namespace face_manager {

static constexpr int N_FACES = 3;            // LCD + mech + term
static constexpr const char* NVS_NS  = "face";
static constexpr const char* NVS_KEY = "idx";

static lv_obj_t* s_tileview         = nullptr;
static lv_obj_t* s_tiles[N_FACES]   = {};
static int       s_active_idx       = 0;

static void on_value_changed(lv_event_t* e) {
    lv_obj_t* tv   = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* tile = lv_tileview_get_tile_active(tv);
    for (int i = 0; i < N_FACES; i++) {
        if (s_tiles[i] == tile) {
            if (s_active_idx != i) {
                s_active_idx = i;
                Preferences p;
                p.begin(NVS_NS, false);
                p.putInt(NVS_KEY, i);
                p.end();
                Serial.printf("[face] switched to tile %d\n", i);
            }
            break;
        }
    }
}

void create() {
    // Restore last-shown tile
    {
        Preferences p;
        p.begin(NVS_NS, true);
        s_active_idx = p.getInt(NVS_KEY, 0);
        p.end();
        if (s_active_idx < 0 || s_active_idx >= N_FACES) s_active_idx = 0;
    }

    lv_obj_t* scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    s_tileview = lv_tileview_create(scr);
    lv_obj_remove_style_all(s_tileview);
    lv_obj_set_size(s_tileview, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_tileview, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_tileview, LV_OPA_COVER, 0);

    for (int i = 0; i < N_FACES; i++) {
        s_tiles[i] = lv_tileview_add_tile(s_tileview, i, 0, LV_DIR_HOR);
    }

    // Tile 0: Casio LCD
    face_lcd::create(s_tiles[0]);
    face_lcd::update();

    // Tile 1: mechanical analog watch (LILYGO hands)
    face_mech::create(s_tiles[1]);

    // Tile 2: cyber terminal (VT323 phosphor green)
    face_term::create(s_tiles[2]);

    lv_tileview_set_tile(s_tileview, s_tiles[s_active_idx], LV_ANIM_OFF);
    lv_obj_add_event_cb(s_tileview, on_value_changed, LV_EVENT_VALUE_CHANGED, nullptr);

    Serial.printf("[face] manager up, %d tiles, active=%d\n", N_FACES, s_active_idx);
}

void update() {
    // Cheap to refresh all three each second; only the active tile is visible.
    face_lcd::update();
    face_mech::update();
    face_term::update();
}

int currentIndex() { return s_active_idx; }

}  // namespace face_manager
