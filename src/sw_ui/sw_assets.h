// Mechanical-chronograph Stopwatch skin (design by Manus AI, 2026-06-03).
// PNG components → LVGL images via LVGLImage.py; all text stays LVGL-drawn.
//   sw_bg            466x466 RGB565   dial + glass highlight (composited)
//   sw_time_window   368x96  RGB565A8 glass time crystal (digits drawn on top)
//   sw_btn_green/red/gold 150x62 RGB565A8 button faces (label drawn on top)
//   sw_lap_panel     344x132 RGB565A8 lap board (rows drawn on top)
//   sw_badge_*       118x24  RGB565A8 status pill + LED (word drawn on top)
#pragma once
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_image_dsc_t sw_bg;
extern const lv_image_dsc_t sw_time_window;
extern const lv_image_dsc_t sw_btn_green;     // START / RESUME
extern const lv_image_dsc_t sw_btn_red;       // STOP
extern const lv_image_dsc_t sw_btn_gold;      // LAP / RESET
extern const lv_image_dsc_t sw_lap_panel;
extern const lv_image_dsc_t sw_badge_ready;
extern const lv_image_dsc_t sw_badge_running;
extern const lv_image_dsc_t sw_badge_paused;

#ifdef __cplusplus
}
#endif
