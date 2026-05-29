// LVGL 9.x configuration for M5StopWatch (468×468 round AMOLED, ESP32-S3 + 8MB PSRAM)
// Located in include/ so it's picked up via -DLV_CONF_INCLUDE_SIMPLE
#pragma once

// ── color & memory ───────────────────────────────────────
#define LV_COLOR_DEPTH        16
#define LV_COLOR_16_SWAP      1      // CO5300 expects byte-swapped RGB565

#define LV_USE_STDLIB_MALLOC  LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING  LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN

#define LV_MEM_SIZE           (96U * 1024U)
#define LV_MEM_POOL_INCLUDE   <stdlib.h>

// ── tick & log ───────────────────────────────────────────
#define LV_TICK_CUSTOM        0      // we call lv_tick_inc() ourselves from the main loop
#define LV_USE_LOG            1
#define LV_LOG_LEVEL          LV_LOG_LEVEL_INFO
#define LV_LOG_PRINTF         1
#define LV_USE_ASSERT_NULL    1

// no perf/mem overlay
#define LV_USE_PERF_MONITOR   0
#define LV_USE_MEM_MONITOR    0

// ── fonts (built-in, more added later via fontconverter) ─
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT       &lv_font_montserrat_18

// ── widgets we'll use ────────────────────────────────────
#define LV_USE_LABEL          1
#define LV_USE_BUTTON         1
#define LV_USE_IMAGE          1
#define LV_USE_LINE           1
#define LV_USE_CANVAS         1
#define LV_USE_TILEVIEW       1
#define LV_USE_ARC            1
#define LV_USE_BAR            1
#define LV_USE_ANIMIMG        1

// ── theme ────────────────────────────────────────────────
#define LV_USE_THEME_DEFAULT  1
#define LV_THEME_DEFAULT_DARK 1

// ── drawing ──────────────────────────────────────────────
#define LV_USE_DRAW_SW        1
