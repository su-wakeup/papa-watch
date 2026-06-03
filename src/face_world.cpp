#include "face_world.h"
#include "tz_helper.h"
#include <lvgl.h>
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <time.h>
#include <math.h>
#include <stdio.h>

LV_FONT_DECLARE(mont_light_14);

namespace face_world {

// name, latitude (°N), longitude (°E), POSIX TZ (DST baked in), focal
struct City { const char* name; float lat; float lon; const char* tz; bool focal; };
static const City CITIES[] = {
    { "Beijing",   39.9f,  116.4f, "CST-8",                        true  },   // PAPA
    { "Los Gatos", 37.2f, -121.9f, "PST8PDT,M3.2.0,M11.1.0",       false },   // Stanley
    { "New York",  40.7f,  -74.0f, "EST5EDT,M3.2.0,M11.1.0",       false },
    { "London",    51.5f,   -0.1f, "GMT0BST,M3.5.0/1,M10.5.0",     false },
    { "Tokyo",     35.7f,  139.7f, "JST-9",                        false },
    { "Sydney",   -33.9f,  151.2f, "AEST-10AEDT,M10.1.0,M4.1.0/3", false },
};
static constexpr int NC = sizeof(CITIES) / sizeof(CITIES[0]);

static constexpr int   CANVAS_SZ = 372;          // even → row stride (×2) is 4-aligned
static constexpr int   CC        = CANVAS_SZ / 2; // canvas centre px
static constexpr float R         = 178.0f;       // globe radius (px)
static constexpr float ROT_STEP  = 20.0f;        // degrees per A/B press
static constexpr float D2R       = 0.01745329252f;

static lv_obj_t*  s_root         = nullptr;
static lv_obj_t*  s_canvas       = nullptr;
static uint16_t*  s_buf          = nullptr;       // RGB565, CANVAS_SZ²
static lv_obj_t*  s_city_lbl[NC] = {};
static float      s_lon0         = 116.4f;        // meridian facing the viewer

static constexpr uint32_t COL_BG    = 0x100A05;   // space (matches tile bg)
static constexpr uint32_t COL_OCEAN = 0x0E2233;   // sea
static constexpr uint32_t COL_GRID  = 0x244A5A;   // meridians / parallels
static constexpr uint32_t COL_EQ    = 0x3A6378;   // equator (brighter)

static inline uint16_t rgb565(uint32_t c) {
    uint8_t r = c >> 16, g = c >> 8, b = c;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
static inline void px(int x, int y, uint32_t c) {
    if ((unsigned)x < CANVAS_SZ && (unsigned)y < CANVAS_SZ)
        s_buf[y * CANVAS_SZ + x] = rgb565(c);
}

// Orthographic projection (north up, equator horizontal). Returns false for the
// far hemisphere (points behind the globe).
static bool project(float lat, float lon, int* ox, int* oy) {
    float phi = lat * D2R;
    float dl  = (lon - s_lon0) * D2R;
    if (cosf(phi) * cosf(dl) <= 0.0f) return false;      // behind the limb
    *ox = CC + (int)lroundf( R * cosf(phi) * sinf(dl));
    *oy = CC + (int)lroundf(-R * sinf(phi));
    return true;
}

static void drawGlobe() {
    // space background
    for (int i = 0; i < CANVAS_SZ * CANVAS_SZ; i++) s_buf[i] = rgb565(COL_BG);

    // ocean disc
    const int Ri = (int)R;
    for (int dy = -Ri; dy <= Ri; dy++) {
        int span = (int)sqrtf((float)(Ri * Ri - dy * dy));
        for (int dx = -span; dx <= span; dx++) px(CC + dx, CC + dy, COL_OCEAN);
    }

    // meridians every 30°
    for (int lon = -180; lon < 180; lon += 30)
        for (float lat = -90; lat <= 90; lat += 1.2f) {
            int x, y; if (project(lat, lon, &x, &y)) px(x, y, COL_GRID);
        }
    // parallels every 30° (equator brighter)
    for (int lat = -60; lat <= 60; lat += 30) {
        uint32_t c = (lat == 0) ? COL_EQ : COL_GRID;
        for (float lon = -180; lon < 180; lon += 0.8f) {
            int x, y; if (project((float)lat, lon, &x, &y)) px(x, y, c);
        }
    }

    // city dots
    for (int i = 0; i < NC; i++) {
        int x, y; if (!project(CITIES[i].lat, CITIES[i].lon, &x, &y)) continue;
        uint32_t c = CITIES[i].focal ? 0xE6A050 : 0xF0D8B0;
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++)
                if (dx * dx + dy * dy <= 4) px(x + dx, y + dy, c);
    }

    lv_obj_invalidate(s_canvas);
}

// Reposition + show/hide each city's label and refresh its time. Cheap: no
// canvas redraw, so this is what runs every second.
static void layoutLabels() {
    time_t now = time(nullptr);
    char buf[40];
    for (int i = 0; i < NC; i++) {
        int x, y;
        if (!project(CITIES[i].lat, CITIES[i].lon, &x, &y)) {
            lv_obj_add_flag(s_city_lbl[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(s_city_lbl[i], LV_OBJ_FLAG_HIDDEN);
        struct tm t; tz_helper::localtime_in(CITIES[i].tz, now, &t);
        snprintf(buf, sizeof(buf), "%s %02d:%02d", CITIES[i].name, t.tm_hour, t.tm_min);
        lv_label_set_text(s_city_lbl[i], buf);
        lv_obj_align(s_city_lbl[i], LV_ALIGN_CENTER, (x - CC), (y - CC) - 13);
    }
}

void create(lv_obj_t* parent) {
    s_root = parent;
    lv_obj_set_style_bg_color(s_root, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    s_buf = (uint16_t*)heap_caps_malloc(CANVAS_SZ * CANVAS_SZ * 2, MALLOC_CAP_SPIRAM);

    s_canvas = lv_canvas_create(s_root);
    if (s_buf) lv_canvas_set_buffer(s_canvas, s_buf, CANVAS_SZ, CANVAS_SZ, LV_COLOR_FORMAT_RGB565);
    lv_obj_align(s_canvas, LV_ALIGN_CENTER, 0, 0);

    for (int i = 0; i < NC; i++) {
        s_city_lbl[i] = lv_label_create(s_root);
        lv_obj_set_style_text_font (s_city_lbl[i], &mont_light_14, 0);
        lv_obj_set_style_text_color(s_city_lbl[i],
            lv_color_hex(CITIES[i].focal ? 0xF2C77A : 0xD8E2EA), 0);
        lv_label_set_text(s_city_lbl[i], "");
    }

    lv_obj_t* hint = lv_label_create(s_root);
    lv_obj_set_style_text_font(hint, &mont_light_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x5A4A30), 0);
    lv_label_set_text(hint, "A/B spin");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -70);

    if (s_buf) drawGlobe();
    layoutLabels();
}

void update() {
    if (!s_root) return;
    layoutLabels();                 // times tick; globe only redraws on rotate
}

void rotate(int dir) {
    if (!s_root || !s_buf) return;
    s_lon0 -= dir * ROT_STEP;        // +1 (BtnB) spins the Earth east-to-west under us
    if (s_lon0 >= 180.0f) s_lon0 -= 360.0f;
    if (s_lon0 < -180.0f) s_lon0 += 360.0f;
    drawGlobe();
    layoutLabels();
}

void destroy() {
    s_root = s_canvas = nullptr;
    for (int i = 0; i < NC; i++) s_city_lbl[i] = nullptr;
    if (s_buf) { heap_caps_free(s_buf); s_buf = nullptr; }
}

}  // namespace face_world
