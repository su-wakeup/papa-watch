// Launcher app icons — A8 LVGL images rendered from the Phosphor glyphs (see
// scripts/render_icons.py). Interim set; the custom PNGs in assets/icons/ drop
// in by re-running the render/convert step. White alpha masks → recoloured at
// runtime (amber center, dim sides).
#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Rich skin — full-colour RGB565A8.
extern const lv_image_dsc_t icon_watch;
extern const lv_image_dsc_t icon_stopwatch;
extern const lv_image_dsc_t icon_schedule;
extern const lv_image_dsc_t icon_aichat;
extern const lv_image_dsc_t icon_papa;
extern const lv_image_dsc_t icon_compass;
extern const lv_image_dsc_t icon_settings;

// Simple skin — mono A8 Phosphor glyphs, recoloured at runtime.
extern const lv_image_dsc_t icon_watch_mono;
extern const lv_image_dsc_t icon_stopwatch_mono;
extern const lv_image_dsc_t icon_schedule_mono;
extern const lv_image_dsc_t icon_aichat_mono;
extern const lv_image_dsc_t icon_papa_mono;
extern const lv_image_dsc_t icon_compass_mono;
extern const lv_image_dsc_t icon_settings_mono;

#ifdef __cplusplus
}
#endif
