#include "app_stopwatch.h"
#include <lvgl.h>
#include <esp_timer.h>
#include <stdio.h>

LV_FONT_DECLARE(mont_light_14);
LV_FONT_DECLARE(mont_light_20);
LV_FONT_DECLARE(mont_light_24);
LV_FONT_DECLARE(mont_light_72_digits);

namespace app_stopwatch {

enum State { IDLE, RUNNING, PAUSED };

// ── persistent state (survives enter/leave so timer can run in background) ──
static State    s_state     = IDLE;
static int64_t  s_run_started_us = 0;     // monotonic time when current RUN segment began
static int64_t  s_accumulated_us = 0;     // elapsed from previous segments (when paused)
static constexpr int MAX_LAPS = 20;
static int64_t  s_laps_us[MAX_LAPS] = {};
static int      s_lap_count = 0;

// ── widget refs (rebuilt on every enter) ────────────────────────────────────
static lv_obj_t* s_root        = nullptr;
static lv_obj_t* s_hero_pill   = nullptr;
static lv_obj_t* s_time_lbl    = nullptr;
static lv_obj_t* s_status_lbl  = nullptr;
static lv_obj_t* s_btn_run     = nullptr;
static lv_obj_t* s_btn_run_lbl = nullptr;
static lv_obj_t* s_btn_aux     = nullptr;
static lv_obj_t* s_btn_aux_lbl = nullptr;
static lv_obj_t* s_lap_list    = nullptr;

static int64_t elapsed_us() {
    if (s_state == RUNNING) {
        return s_accumulated_us + (esp_timer_get_time() - s_run_started_us);
    }
    return s_accumulated_us;
}

// Format microseconds as "MM:SS.cc" (centiseconds). Cap minutes at 99 since
// we only have 8 chars worth of hero space; longer sessions overflow visually
// but the lap math is still correct.
static void format_mmsscc(char* buf, size_t n, int64_t us) {
    int64_t cs_total = us / 10000;            // centiseconds
    int mm = (int)((cs_total / 6000) % 100);
    int ss = (int)((cs_total / 100) % 60);
    int cc = (int)(cs_total % 100);
    snprintf(buf, n, "%02d:%02d.%02d", mm, ss, cc);
}

static void render_lap_row(int idx, int64_t lap_us) {
    // Show lap time + delta from previous lap. Most-recent row in warm amber,
    // older rows fade to sepia tan so the eye lands on the latest split.
    char buf[48], t[16];
    format_mmsscc(t, sizeof(t), lap_us);
    if (idx > 1) {
        char d[16];
        int64_t prev = s_laps_us[idx - 2];
        int64_t delta = lap_us - prev;
        format_mmsscc(d, sizeof(d), delta < 0 ? -delta : delta);
        snprintf(buf, sizeof(buf), "L%-2d  %s   +%s", idx, t, d);
    } else {
        snprintf(buf, sizeof(buf), "L%-2d  %s", idx, t);
    }
    lv_obj_t* row = lv_list_add_text(s_lap_list, buf);
    lv_obj_set_style_text_font(row, &mont_light_20, 0);
    bool latest = (idx == s_lap_count);
    lv_obj_set_style_text_color(row,
        latest ? lv_color_hex(0xE6A050) : lv_color_hex(0x9C8870), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_top(row, 3, 0);
    lv_obj_set_style_pad_bottom(row, 3, 0);
    lv_obj_set_style_pad_left(row, 14, 0);
}

static void refresh_lap_list() {
    if (!s_lap_list) return;
    lv_obj_clean(s_lap_list);
    for (int i = 0; i < s_lap_count; i++) {
        render_lap_row(i + 1, s_laps_us[i]);
    }
    // pin to most-recent lap
    lv_obj_scroll_to_y(s_lap_list, 99999, LV_ANIM_OFF);
}

static void update_button_labels() {
    if (!s_btn_run_lbl) return;
    switch (s_state) {
        case IDLE:
            lv_label_set_text(s_btn_run_lbl, "START");
            lv_obj_set_style_bg_color(s_btn_run, lv_color_hex(0x6FAB52), 0);
            lv_obj_set_style_shadow_color(s_btn_run, lv_color_hex(0x6FAB52), 0);
            if (s_status_lbl) {
                lv_label_set_text(s_status_lbl, "READY");
                lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0x705840), 0);
            }
            lv_label_set_text(s_btn_aux_lbl, "LAP");
            lv_obj_add_flag(s_btn_aux, LV_OBJ_FLAG_HIDDEN);
            break;
        case RUNNING:
            lv_label_set_text(s_btn_run_lbl, "STOP");
            lv_obj_set_style_bg_color(s_btn_run, lv_color_hex(0xC04030), 0);
            lv_obj_set_style_shadow_color(s_btn_run, lv_color_hex(0xC04030), 0);
            if (s_status_lbl) {
                lv_label_set_text(s_status_lbl, "RUNNING");
                lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0x80D070), 0);
            }
            lv_label_set_text(s_btn_aux_lbl, "LAP");
            lv_obj_remove_flag(s_btn_aux, LV_OBJ_FLAG_HIDDEN);
            break;
        case PAUSED:
            lv_label_set_text(s_btn_run_lbl, "RESUME");
            lv_obj_set_style_bg_color(s_btn_run, lv_color_hex(0x6FAB52), 0);
            lv_obj_set_style_shadow_color(s_btn_run, lv_color_hex(0x6FAB52), 0);
            if (s_status_lbl) {
                lv_label_set_text(s_status_lbl, "PAUSED");
                lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0xE6A050), 0);
            }
            lv_label_set_text(s_btn_aux_lbl, "RESET");
            lv_obj_remove_flag(s_btn_aux, LV_OBJ_FLAG_HIDDEN);
            break;
    }
}

void press_run() {
    switch (s_state) {
        case IDLE:
        case PAUSED:
            s_run_started_us = esp_timer_get_time();
            s_state = RUNNING;
            break;
        case RUNNING:
            s_accumulated_us += esp_timer_get_time() - s_run_started_us;
            s_state = PAUSED;
            break;
    }
    update_button_labels();
}

void press_aux() {
    if (s_state == RUNNING) {
        // lap snapshot — does NOT pause the timer
        if (s_lap_count < MAX_LAPS) {
            s_laps_us[s_lap_count++] = elapsed_us();
            if (s_lap_list) {
                render_lap_row(s_lap_count, s_laps_us[s_lap_count - 1]);
                lv_obj_scroll_to_y(s_lap_list, 99999, LV_ANIM_OFF);
            }
        }
    } else if (s_state == PAUSED) {
        // full reset back to zero
        s_accumulated_us = 0;
        s_lap_count = 0;
        s_state = IDLE;
        refresh_lap_list();
        update_button_labels();
    }
}

static void on_run_clicked(lv_event_t*) { press_run(); }
static void on_aux_clicked(lv_event_t*) { press_aux(); }

void enter(lv_obj_t* parent) {
    s_root = parent;
    // Warm dark sepia background ties the stopwatch to the watch's "old leather
    // & brass" palette — same family as the mechanical face.
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0x100A05), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);

    // Status badge above the hero number: READY / RUNNING / PAUSED with
    // colour cue so kid sees state at a glance.
    s_status_lbl = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_status_lbl, &mont_light_14, 0);
    lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0x705840), 0);
    lv_label_set_text(s_status_lbl, "READY");
    lv_obj_align(s_status_lbl, LV_ALIGN_CENTER, 0, -170);

    // Hero "screen well" — slightly lifted rounded slab behind the digits so
    // the time reads like a watch crystal, not bare text on void.
    s_hero_pill = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_hero_pill);
    lv_obj_set_size(s_hero_pill, 380, 100);
    lv_obj_align(s_hero_pill, LV_ALIGN_CENTER, 0, -115);
    lv_obj_set_style_bg_color(s_hero_pill, lv_color_hex(0x1F1308), 0);
    lv_obj_set_style_bg_opa(s_hero_pill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_hero_pill, 18, 0);
    lv_obj_set_style_border_color(s_hero_pill, lv_color_hex(0x3A2818), 0);
    lv_obj_set_style_border_width(s_hero_pill, 1, 0);
    lv_obj_set_style_shadow_width(s_hero_pill, 18, 0);
    lv_obj_set_style_shadow_color(s_hero_pill, lv_color_hex(0xE6A050), 0);
    lv_obj_set_style_shadow_opa(s_hero_pill, 32, 0);   // ~12%
    lv_obj_set_style_shadow_spread(s_hero_pill, 0, 0);
    lv_obj_remove_flag(s_hero_pill, LV_OBJ_FLAG_SCROLLABLE);

    s_time_lbl = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_time_lbl, &mont_light_72_digits, 0);
    lv_obj_set_style_text_color(s_time_lbl, lv_color_hex(0xF5E8D0), 0);
    lv_label_set_text(s_time_lbl, "00:00.00");
    lv_obj_align(s_time_lbl, LV_ALIGN_CENTER, 0, -115);

    // Control buttons — rounded chunky, glow-shadow tinted to match the
    // bg colour so they pop without looking like LVGL stock blue.
    s_btn_run = lv_button_create(s_root);
    lv_obj_set_size(s_btn_run, 140, 56);
    lv_obj_align(s_btn_run, LV_ALIGN_CENTER, -82, -10);
    lv_obj_set_style_radius(s_btn_run, 28, 0);
    lv_obj_set_style_bg_opa(s_btn_run, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_btn_run, 0, 0);
    lv_obj_set_style_shadow_width(s_btn_run, 18, 0);
    lv_obj_set_style_shadow_opa(s_btn_run, 80, 0);
    lv_obj_set_style_shadow_spread(s_btn_run, 0, 0);
    lv_obj_add_event_cb(s_btn_run, on_run_clicked, LV_EVENT_CLICKED, nullptr);
    s_btn_run_lbl = lv_label_create(s_btn_run);
    lv_obj_set_style_text_font(s_btn_run_lbl, &mont_light_24, 0);
    lv_obj_set_style_text_color(s_btn_run_lbl, lv_color_hex(0x0A0805), 0);
    lv_obj_center(s_btn_run_lbl);

    s_btn_aux = lv_button_create(s_root);
    lv_obj_set_size(s_btn_aux, 140, 56);
    lv_obj_align(s_btn_aux, LV_ALIGN_CENTER, 82, -10);
    lv_obj_set_style_radius(s_btn_aux, 28, 0);
    lv_obj_set_style_bg_color(s_btn_aux, lv_color_hex(0x2A1B0E), 0);
    lv_obj_set_style_bg_opa(s_btn_aux, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_btn_aux, 1, 0);
    lv_obj_set_style_border_color(s_btn_aux, lv_color_hex(0x6A4A2C), 0);
    lv_obj_add_event_cb(s_btn_aux, on_aux_clicked, LV_EVENT_CLICKED, nullptr);
    s_btn_aux_lbl = lv_label_create(s_btn_aux);
    lv_obj_set_style_text_font(s_btn_aux_lbl, &mont_light_24, 0);
    lv_obj_set_style_text_color(s_btn_aux_lbl, lv_color_hex(0xE6A050), 0);
    lv_obj_center(s_btn_aux_lbl);

    // Lap list at the bottom — transparent so it sits on the sepia bg, no
    // visible scrollbar; latest row gets the warm amber from render_lap_row.
    s_lap_list = lv_list_create(s_root);
    lv_obj_set_size(s_lap_list, 320, 130);
    lv_obj_align(s_lap_list, LV_ALIGN_CENTER, 0, 110);
    lv_obj_set_style_bg_opa(s_lap_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_lap_list, 0, 0);
    lv_obj_set_style_pad_all(s_lap_list, 0, 0);
    lv_obj_set_scrollbar_mode(s_lap_list, LV_SCROLLBAR_MODE_OFF);

    update_button_labels();
    refresh_lap_list();
}

void leave() {
    if (s_root) {
        lv_obj_clean(s_root);
        s_root = nullptr;
    }
    s_time_lbl    = nullptr;
    s_hero_pill   = nullptr;
    s_status_lbl  = nullptr;
    s_btn_run     = s_btn_run_lbl = nullptr;
    s_btn_aux     = s_btn_aux_lbl = nullptr;
    s_lap_list    = nullptr;
    // s_state, s_run_started_us, s_accumulated_us, s_laps_us stay — the
    // stopwatch keeps running in the background after the user leaves.
}

void tick() {
    if (!s_time_lbl) return;
    if (s_state != RUNNING) return;      // paused/idle don't need redraws
    char buf[16];
    format_mmsscc(buf, sizeof(buf), elapsed_us());
    lv_label_set_text(s_time_lbl, buf);
}

}  // namespace app_stopwatch
