// Mechanical sundial / astrolabe skin (design by Manus AI, 2026-06-03).
// Static mechanical layers (dial + compass letters + brass ring + gnomon base +
// status ribbon plate + glass reflection) are pre-composited into sd_bg to fit
// flash. Only the live layers stay separate: the solar shadow line (rotates by
// sun azimuth), the bubble (moves with IMU tilt), its lens base + glass highlight.
#pragma once
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_image_dsc_t sd_bg;          // 466x466 composited static base
extern const lv_image_dsc_t sd_shadow;      // 214x34 solar shadow line (pivot 17,17)
extern const lv_image_dsc_t sd_bubble_base; // 132x132 level lens liquid
extern const lv_image_dsc_t sd_bubble;      // 56x56 air bubble (moves)
extern const lv_image_dsc_t sd_bubble_hi;   // 132x132 glass highlight (over bubble)

#ifdef __cplusplus
}
#endif
