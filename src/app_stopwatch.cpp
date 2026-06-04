#include "app_stopwatch.h"
#include "sw_ui/sw_assets.h"
#include "fs_image.h"
#include <lvgl.h>
#include <esp_timer.h>
#include <stdio.h>

LV_FONT_DECLARE(mont_light_20);
LV_FONT_DECLARE(mont_light_24);
LV_FONT_DECLARE(mont_light_48_digits);   // sized to fit the 368x96 glass window

namespace app_stopwatch {

enum State { IDLE, RUNNING, PAUSED };

// ── persistent state (survives enter/leave so timer can run in background) ──
static State    s_state     = IDLE;
static int64_t  s_run_started_us = 0;     // monotonic time when current RUN segment began
static int64_t  s_accumulated_us = 0;     // elapsed from previous segments (when paused)
static constexpr int MAX_LAPS = 20;
static int64_t  s_laps_us[MAX_LAPS] = {};
static int      s_lap_count = 0;

// ── widget refs (rebuilt on every enter) — mechanical-chronograph skin ──────
static constexpr int LAP_ROWS = 4;
static lv_obj_t* s_root        = nullptr;
static lv_obj_t* s_badge       = nullptr;   // status pill + LED (image swapped by state)
static lv_obj_t* s_status_lbl  = nullptr;   // READY / RUNNING / PAUSED text on the pill
static lv_obj_t* s_time_lbl    = nullptr;   // big MM:SS.cc in the glass window
static lv_obj_t* s_btn_left    = nullptr;   // START / STOP / RESUME (image)
static lv_obj_t* s_btn_left_lbl= nullptr;
static lv_obj_t* s_btn_right   = nullptr;   // LAP / RESET (image)
static lv_obj_t* s_btn_right_lbl=nullptr;
static lv_obj_t* s_lap_panel   = nullptr;   // lap board image
static lv_obj_t* s_lap_row[LAP_ROWS] = {};  // up to 4 most-recent laps
static lv_image_dsc_t* s_bg_dsc = nullptr;  // dial bg loaded from LittleFS → PSRAM

static int64_t elapsed_us() {
    if (s_state == RUNNING) {
        return s_accumulated_us + (esp_timer_get_time() - s_run_started_us);
    }
    return s_accumulated_us;
}

// Format microseconds as "MM:SS.cc" (centiseconds). Minutes cap at 99.
static void format_mmsscc(char* buf, size_t n, int64_t us) {
    int64_t cs_total = us / 10000;
    int mm = (int)((cs_total / 6000) % 100);
    int ss = (int)((cs_total / 100) % 60);
    int cc = (int)(cs_total % 100);
    snprintf(buf, n, "%02d:%02d.%02d", mm, ss, cc);
}

// Show the most-recent LAP_ROWS laps on the board; newest brightest.
static void refresh_laps() {
    if (!s_lap_row[0]) return;
    int start = s_lap_count > LAP_ROWS ? s_lap_count - LAP_ROWS : 0;
    int shown = s_lap_count - start;
    char t[16], d[16], buf[40];
    for (int r = 0; r < LAP_ROWS; r++) {
        if (r >= shown) { lv_obj_add_flag(s_lap_row[r], LV_OBJ_FLAG_HIDDEN); continue; }
        int idx = start + r;                          // 0-based lap
        format_mmsscc(t, sizeof(t), s_laps_us[idx]);
        if (idx > 0) {                                // delta vs the previous lap
            int64_t delta = s_laps_us[idx] - s_laps_us[idx - 1];
            format_mmsscc(d, sizeof(d), delta < 0 ? -delta : delta);
            snprintf(buf, sizeof(buf), "L%-2d %s  %c%s", idx + 1, t, delta < 0 ? '-' : '+', d);
        } else {
            snprintf(buf, sizeof(buf), "L%-2d %s", idx + 1, t);
        }
        lv_label_set_text(s_lap_row[r], buf);
        bool latest = (idx == s_lap_count - 1);
        lv_obj_set_style_text_color(s_lap_row[r],
            lv_color_hex(latest ? 0xFFE8AA : 0xDAB466), 0);
        lv_obj_remove_flag(s_lap_row[r], LV_OBJ_FLAG_HIDDEN);
    }
}

// Drive every image + label off the current state (the design's state table).
static void apply_state() {
    if (!s_badge) return;
    bool show_right = (s_state != IDLE);
    bool show_laps  = (s_state != IDLE);

    switch (s_state) {
        case IDLE:
            lv_image_set_src(s_badge, &sw_badge_ready);
            lv_label_set_text(s_status_lbl, "READY");
            lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0xECD28F), 0);
            lv_image_set_src(s_btn_left, &sw_btn_green);
            lv_label_set_text(s_btn_left_lbl, "START");
            break;
        case RUNNING:
            lv_image_set_src(s_badge, &sw_badge_running);
            lv_label_set_text(s_status_lbl, "RUNNING");
            lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0x94FF6D), 0);
            lv_image_set_src(s_btn_left, &sw_btn_red);
            lv_label_set_text(s_btn_left_lbl, "STOP");
            lv_image_set_src(s_btn_right, &sw_btn_gold);
            lv_label_set_text(s_btn_right_lbl, "LAP");
            break;
        case PAUSED:
            lv_image_set_src(s_badge, &sw_badge_paused);
            lv_label_set_text(s_status_lbl, "PAUSED");
            lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0xFFB95B), 0);
            lv_image_set_src(s_btn_left, &sw_btn_green);
            lv_label_set_text(s_btn_left_lbl, "RESUME");
            lv_image_set_src(s_btn_right, &sw_btn_gold);
            lv_label_set_text(s_btn_right_lbl, "RESET");
            break;
    }
    if (show_right) { lv_obj_remove_flag(s_btn_right, LV_OBJ_FLAG_HIDDEN); }
    else            { lv_obj_add_flag   (s_btn_right, LV_OBJ_FLAG_HIDDEN); }
    if (show_laps)  { lv_obj_remove_flag(s_lap_panel, LV_OBJ_FLAG_HIDDEN); }
    else            { lv_obj_add_flag   (s_lap_panel, LV_OBJ_FLAG_HIDDEN); }
    refresh_laps();
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
    apply_state();
}

void press_aux() {
    if (s_state == RUNNING) {
        if (s_lap_count < MAX_LAPS) {       // lap snapshot — does NOT pause
            s_laps_us[s_lap_count++] = elapsed_us();
            refresh_laps();
        }
    } else if (s_state == PAUSED) {
        s_accumulated_us = 0;               // full reset to zero
        s_lap_count = 0;
        s_state = IDLE;
        apply_state();
        if (s_time_lbl) lv_label_set_text(s_time_lbl, "00:00.00");
    }
}

static void on_run_clicked(lv_event_t*) { press_run(); }
static void on_aux_clicked(lv_event_t*) { press_aux(); }

// Helper: a centred label that sits on top of an image button.
static lv_obj_t* btn_label(lv_obj_t* img, uint32_t color) {
    lv_obj_t* l = lv_label_create(img);
    lv_obj_set_style_text_font(l, &mont_light_24, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_center(l);
    return l;
}

void enter(lv_obj_t* parent) {
    s_root = parent;
    lv_obj_set_style_bg_color(s_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    // Layer 0 — full-screen mechanical dial, streamed from LittleFS into PSRAM
    // (kept out of the app flash partition).
    s_bg_dsc = fs_image::load_rgb565("/ui/sw_bg.565", 466, 466);
    lv_obj_t* bg = lv_image_create(s_root);
    if (s_bg_dsc) lv_image_set_src(bg, s_bg_dsc);
    lv_obj_set_pos(bg, 0, 0);

    // Status pill + word.
    s_badge = lv_image_create(s_root);
    lv_obj_set_pos(s_badge, 174, 42);
    s_status_lbl = lv_label_create(s_badge);
    lv_obj_set_style_text_font(s_status_lbl, &mont_light_20, 0);
    lv_obj_align(s_status_lbl, LV_ALIGN_CENTER, 10, 0);   // nudge right of the LED

    // Glass time crystal + big digits.
    lv_obj_t* win = lv_image_create(s_root);
    lv_image_set_src(win, &sw_time_window);
    lv_obj_set_pos(win, 49, 73);
    s_time_lbl = lv_label_create(win);
    lv_obj_set_style_text_font(s_time_lbl, &mont_light_48_digits, 0);
    lv_obj_set_style_text_color(s_time_lbl, lv_color_hex(0xFFF9E2), 0);
    lv_label_set_text(s_time_lbl, "00:00.00");
    lv_obj_center(s_time_lbl);

    // Left control (START/STOP/RESUME) — clickable image.
    s_btn_left = lv_image_create(s_root);
    lv_obj_set_pos(s_btn_left, 73, 190);
    lv_obj_add_flag(s_btn_left, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_btn_left, on_run_clicked, LV_EVENT_CLICKED, nullptr);
    s_btn_left_lbl = btn_label(s_btn_left, 0xFFF7DA);

    // Right control (LAP/RESET).
    s_btn_right = lv_image_create(s_root);
    lv_obj_set_pos(s_btn_right, 243, 190);
    lv_obj_add_flag(s_btn_right, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_btn_right, on_aux_clicked, LV_EVENT_CLICKED, nullptr);
    s_btn_right_lbl = btn_label(s_btn_right, 0xFFF7DA);

    // Lap board + 4 rows.
    s_lap_panel = lv_image_create(s_root);
    lv_image_set_src(s_lap_panel, &sw_lap_panel);
    lv_obj_set_pos(s_lap_panel, 61, 280);
    for (int r = 0; r < LAP_ROWS; r++) {
        s_lap_row[r] = lv_label_create(s_lap_panel);
        lv_obj_set_style_text_font(s_lap_row[r], &mont_light_20, 0);
        lv_obj_set_style_text_color(s_lap_row[r], lv_color_hex(0xDAB466), 0);
        lv_obj_set_pos(s_lap_row[r], 30, 16 + r * 27);
        lv_label_set_text(s_lap_row[r], "");
    }

    apply_state();
    // restore the live time on re-enter (timer may have run in the background)
    char buf[16]; format_mmsscc(buf, sizeof(buf), elapsed_us());
    lv_label_set_text(s_time_lbl, buf);
}

void leave() {
    if (s_root) { lv_obj_clean(s_root); s_root = nullptr; }
    fs_image::free(s_bg_dsc); s_bg_dsc = nullptr;   // release the PSRAM bg
    s_badge = s_status_lbl = s_time_lbl = nullptr;
    s_btn_left = s_btn_left_lbl = s_btn_right = s_btn_right_lbl = nullptr;
    s_lap_panel = nullptr;
    for (int r = 0; r < LAP_ROWS; r++) s_lap_row[r] = nullptr;
    // s_state / timers / laps persist — the stopwatch keeps running in background.
}

void tick() {
    if (!s_time_lbl) return;
    if (s_state != RUNNING) return;
    char buf[16];
    format_mmsscc(buf, sizeof(buf), elapsed_us());
    lv_label_set_text(s_time_lbl, buf);
}

}  // namespace app_stopwatch
