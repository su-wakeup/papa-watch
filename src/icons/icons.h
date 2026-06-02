// Launcher app icons — A8 LVGL images rendered from the Phosphor glyphs (see
// scripts/render_icons.py). Interim set; the custom PNGs in assets/icons/ drop
// in by re-running the render/convert step. White alpha masks → recoloured at
// runtime (amber center, dim sides).
#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_image_dsc_t icon_watch;
extern const lv_image_dsc_t icon_stopwatch;
extern const lv_image_dsc_t icon_schedule;
extern const lv_image_dsc_t icon_aichat;
extern const lv_image_dsc_t icon_papa;
extern const lv_image_dsc_t icon_compass;
extern const lv_image_dsc_t icon_settings;

#ifdef __cplusplus
}
#endif
