#include "face_world.h"
#include "tz_helper.h"
#include "world_mask.h"
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
    { "Los Gatos", 37.2f, -121.9f, "PST8PDT,M3.2.0,M11.1.0",       true  },   // Stanley
    // Asia
    { "Tokyo",     35.7f,  139.7f, "JST-9",                        false },
    { "Seoul",     37.6f,  127.0f, "KST-9",                        false },
    { "Hong Kong", 22.3f,  114.2f, "HKT-8",                        false },
    { "Singapore",  1.35f, 103.8f, "SGT-8",                        false },
    { "Bangkok",   13.8f,  100.5f, "ICT-7",                        false },
    { "Mumbai",    19.1f,   72.9f, "IST-5:30",                     false },
    { "Dubai",     25.2f,   55.3f, "GST-4",                        false },
    // Europe
    { "Moscow",    55.8f,   37.6f, "MSK-3",                        false },
    { "Istanbul",  41.0f,   29.0f, "TRT-3",                        false },
    { "London",    51.5f,   -0.1f, "GMT0BST,M3.5.0/1,M10.5.0",     false },
    { "Paris",     48.9f,    2.35f,"CET-1CEST,M3.5.0,M10.5.0/3",   false },
    { "Rome",      41.9f,   12.5f, "CET-1CEST,M3.5.0,M10.5.0/3",   false },
    // Africa
    { "Cairo",     30.0f,   31.2f, "EET-2EEST,M4.5.5/0,M10.5.4/24",false },
    { "Nairobi",   -1.3f,   36.8f, "EAT-3",                        false },
    { "Lagos",      6.5f,    3.4f, "WAT-1",                        false },
    { "Cape Town",-33.9f,   18.4f, "SAST-2",                       false },
    // Americas
    { "New York",  40.7f,  -74.0f, "EST5EDT,M3.2.0,M11.1.0",       false },
    { "Mexico Cty",19.4f,  -99.1f, "CST6",                         false },
    { "Honolulu",  21.3f, -157.9f, "HST10",                        false },
    { "Rio",      -22.9f,  -43.2f, "BRT3",                         false },
    { "Buenos Air",-34.6f, -58.4f, "ART3",                         false },
    // Oceania
    { "Sydney",   -33.9f,  151.2f, "AEST-10AEDT,M10.1.0,M4.1.0/3", false },
    { "Auckland", -36.8f,  174.8f, "NZST-12NZDT,M9.5.0,M4.1.0/3",  false },
};
static constexpr int NC = sizeof(CITIES) / sizeof(CITIES[0]);

static constexpr int   CANVAS_SZ = 372;
static constexpr int   CC        = CANVAS_SZ / 2;
static constexpr float R         = 178.0f;
static constexpr float ROT_STEP  = 20.0f;
static constexpr float D2R       = 0.01745329252f;
static constexpr float R2D       = 57.2957795131f;
static constexpr int16_t OUTSIDE = 0x7FFF;

static lv_obj_t*  s_root         = nullptr;
static lv_obj_t*  s_canvas       = nullptr;
static uint16_t*  s_buf          = nullptr;   // RGB565 canvas
static int16_t*   s_row          = nullptr;   // per-px mask row, OUTSIDE = off-disc
static int16_t*   s_dcol         = nullptr;   // per-px mask column offset from centre meridian
static uint8_t*   s_shade        = nullptr;   // per-px limb darkening 0..255
static lv_obj_t*  s_city_lbl[NC] = {};
static float      s_lon0         = 116.4f;    // meridian facing the viewer
static int        s_last_min     = -1;        // terminator advances on minute change

// Subsolar point (where the sun is overhead), recomputed each redraw.
static float      s_sinDecl = 0, s_cosDecl = 1, s_lonSun = 0;

static constexpr uint32_t COL_BG    = 0x100A05;   // space (matches tile bg)
static constexpr uint32_t COL_OCEAN = 0x103248;
static constexpr uint32_t COL_LAND  = 0x3E6B34;
static constexpr uint32_t COL_GRID  = 0x2A4E5E;   // faint graticule
static constexpr uint32_t COL_EQ    = 0x46708A;

static inline uint16_t rgb565(uint32_t c) {
    uint8_t r = c >> 16, g = c >> 8, b = c;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
static inline uint16_t shade565(uint32_t c, uint8_t s) {
    uint16_t r = (((c >> 16) & 0xFF) * s) >> 8;
    uint16_t g = (((c >>  8) & 0xFF) * s) >> 8;
    uint16_t b = ((c & 0xFF) * s) >> 8;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
static inline void px(int x, int y, uint32_t c) {
    if ((unsigned)x < CANVAS_SZ && (unsigned)y < CANVAS_SZ)
        s_buf[y * CANVAS_SZ + x] = rgb565(c);
}
static inline bool landAt(int row, int col) {
    int idx = row * world_mask::W + col;
    return (world_mask::DATA[idx >> 3] >> (7 - (idx & 7))) & 1;
}

// Forward orthographic (north up, equator horizontal). false = far hemisphere.
static bool project(float lat, float lon, int* ox, int* oy) {
    float phi = lat * D2R, dl = (lon - s_lon0) * D2R;
    if (cosf(phi) * cosf(dl) <= 0.0f) return false;
    *ox = CC + (int)lroundf( R * cosf(phi) * sinf(dl));
    *oy = CC + (int)lroundf(-R * sinf(phi));
    return true;
}

// One-time inverse-projection tables: each on-disc pixel maps to a fixed mask
// row + a longitude offset (independent of rotation) + a limb-shade factor.
static void buildTables() {
    for (int y = 0; y < CANVAS_SZ; y++)
        for (int x = 0; x < CANVAS_SZ; x++) {
            int idx = y * CANVAS_SZ + x;
            float xn = (x - CC) / R, yn = -(y - CC) / R;
            float rho2 = xn * xn + yn * yn;
            if (rho2 > 1.0f) { s_row[idx] = OUTSIDE; continue; }
            float cosc = sqrtf(1.0f - rho2);
            float lat  = asinf(yn) * R2D;
            float dlon = atan2f(xn, cosc) * R2D;
            int row = (int)((90.0f - lat) / world_mask::STEP);
            if (row < 0) row = 0; if (row >= world_mask::H) row = world_mask::H - 1;
            s_row[idx]   = row;
            s_dcol[idx]  = (int16_t)lroundf(dlon / world_mask::STEP);
            s_shade[idx] = (uint8_t)((0.45f + 0.55f * cosc) * 255.0f);
        }
}

// Subsolar declination + longitude (low precision, no equation-of-time — good
// enough that the terminator is right to a few degrees for a kid's globe).
static void computeSun(time_t now) {
    struct tm g; gmtime_r(&now, &g);
    float decl = -23.44f * cosf((2.0f * (float)M_PI / 365.0f) * (g.tm_yday + 10)) * D2R;
    s_sinDecl = sinf(decl);
    s_cosDecl = cosf(decl);
    float utch = g.tm_hour + g.tm_min / 60.0f + g.tm_sec / 3600.0f;
    float ls = 180.0f - 15.0f * utch;
    while (ls >  180.0f) ls -= 360.0f;
    while (ls < -180.0f) ls += 360.0f;
    s_lonSun = ls;
}

static void drawGlobe() {
    const int W = world_mask::W;
    int base = (int)lroundf((s_lon0 + 180.0f) / world_mask::STEP);

    // Day/night: per-column cos(longitude - subsolar longitude), built once.
    computeSun(time(nullptr));
    static float cosLon[world_mask::W];
    for (int c = 0; c < W; c++) {
        float lon = -180.0f + (c + 0.5f) * world_mask::STEP;
        cosLon[c] = cosf((lon - s_lonSun) * D2R);
    }

    for (int y = 0; y < CANVAS_SZ; y++) {
        float sinLat = -(y - CC) / R;                       // = sin(latitude) for this row
        float cosLat = sqrtf(fmaxf(0.0f, 1.0f - sinLat * sinLat));
        for (int x = 0; x < CANVAS_SZ; x++) {
            int idx = y * CANVAS_SZ + x;
            int row = s_row[idx];
            if (row == OUTSIDE) { s_buf[idx] = rgb565(COL_BG); continue; }
            int col = (base + s_dcol[idx]) % W; if (col < 0) col += W;
            // sun zenith cosine at this pixel; >0 = lit
            float cz = sinLat * s_sinDecl + cosLat * s_cosDecl * cosLon[col];
            float day = (cz >= 0.10f) ? 1.0f
                      : (cz <= -0.10f) ? 0.28f
                      : 0.28f + 0.72f * ((cz + 0.10f) / 0.20f);   // soft twilight band
            uint8_t sh = (uint8_t)(s_shade[idx] * day);
            s_buf[idx] = shade565(landAt(row, col) ? COL_LAND : COL_OCEAN, sh);
        }
    }

    // faint graticule over the filled globe
    for (int lon = -180; lon < 180; lon += 30)
        for (float lat = -90; lat <= 90; lat += 1.5f) {
            int x, y; if (project(lat, lon, &x, &y)) px(x, y, COL_GRID);
        }
    for (int lat = -60; lat <= 60; lat += 30) {
        uint32_t c = (lat == 0) ? COL_EQ : COL_GRID;
        for (float lon = -180; lon < 180; lon += 1.0f) {
            int x, y; if (project((float)lat, lon, &x, &y)) px(x, y, c);
        }
    }
    // city dots — every city marked; family cities (PAPA/Stanley) bigger + gold
    for (int i = 0; i < NC; i++) {
        int x, y; if (!project(CITIES[i].lat, CITIES[i].lon, &x, &y)) continue;
        bool f = CITIES[i].focal;
        uint32_t c = f ? 0xFFC257 : 0xFFE6C0;
        int rr = f ? 3 : 2, r2 = rr * rr;
        for (int dy = -rr; dy <= rr; dy++)
            for (int dx = -rr; dx <= rr; dx++)
                if (dx * dx + dy * dy <= r2) px(x + dx, y + dy, c);
    }
    lv_obj_invalidate(s_canvas);
}

// Focus mode: every city is a dot (drawn in drawGlobe), but only the family
// cities plus the one nearest the centre meridian get a name+time label, so the
// globe can hold many cities without the labels ever crowding.
static int s_vx[NC], s_vy[NC];
static bool s_vis[NC];

static void layoutLabels() {
    time_t now = time(nullptr);
    int   focus = -1;
    float bestD2 = 1e18f;
    for (int i = 0; i < NC; i++) {
        s_vis[i] = project(CITIES[i].lat, CITIES[i].lon, &s_vx[i], &s_vy[i]);
        if (s_vis[i] && !CITIES[i].focal) {
            float dx = s_vx[i] - CC, dy = s_vy[i] - CC, d2 = dx * dx + dy * dy;
            if (d2 < bestD2) { bestD2 = d2; focus = i; }
        }
    }
    char buf[40];
    for (int i = 0; i < NC; i++) {
        bool show = s_vis[i] && (CITIES[i].focal || i == focus);
        if (!show) { lv_obj_add_flag(s_city_lbl[i], LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(s_city_lbl[i], LV_OBJ_FLAG_HIDDEN);
        struct tm t; tz_helper::localtime_in(CITIES[i].tz, now, &t);
        snprintf(buf, sizeof(buf), "%s %02d:%02d", CITIES[i].name, t.tm_hour, t.tm_min);
        lv_label_set_text(s_city_lbl[i], buf);
        lv_obj_align(s_city_lbl[i], LV_ALIGN_CENTER, (s_vx[i] - CC), (s_vy[i] - CC) - 15);
    }
}

void create(lv_obj_t* parent) {
    s_root = parent;
    lv_obj_set_style_bg_color(s_root, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    s_buf   = (uint16_t*)heap_caps_malloc(CANVAS_SZ * CANVAS_SZ * 2, MALLOC_CAP_SPIRAM);
    s_row   = (int16_t*) heap_caps_malloc(CANVAS_SZ * CANVAS_SZ * 2, MALLOC_CAP_SPIRAM);
    s_dcol  = (int16_t*) heap_caps_malloc(CANVAS_SZ * CANVAS_SZ * 2, MALLOC_CAP_SPIRAM);
    s_shade = (uint8_t*) heap_caps_malloc(CANVAS_SZ * CANVAS_SZ,     MALLOC_CAP_SPIRAM);
    bool ok = s_buf && s_row && s_dcol && s_shade;

    s_canvas = lv_canvas_create(s_root);
    if (s_buf) lv_canvas_set_buffer(s_canvas, s_buf, CANVAS_SZ, CANVAS_SZ, LV_COLOR_FORMAT_RGB565);
    lv_obj_align(s_canvas, LV_ALIGN_CENTER, 0, 0);

    for (int i = 0; i < NC; i++) {
        s_city_lbl[i] = lv_label_create(s_root);
        lv_obj_set_style_text_font (s_city_lbl[i], &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(s_city_lbl[i],
            lv_color_hex(CITIES[i].focal ? 0xF2C77A : 0xE6EEF4), 0);
        lv_label_set_text(s_city_lbl[i], "");
    }

    lv_obj_t* hint = lv_label_create(s_root);
    lv_obj_set_style_text_font(hint, &mont_light_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x5A4A30), 0);
    lv_label_set_text(hint, "A/B spin");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -64);

    if (ok) { buildTables(); drawGlobe(); }
    layoutLabels();
}

void update() {
    if (!s_root) return;
    layoutLabels();
    // Advance the day/night terminator once a minute (cheap; no need per second).
    struct tm t; time_t now = time(nullptr); localtime_r(&now, &t);
    if (s_buf && t.tm_min != s_last_min) {
        s_last_min = t.tm_min;
        drawGlobe();
    }
}

void rotate(int dir) {
    if (!s_root || !s_buf) return;
    s_lon0 -= dir * ROT_STEP;
    if (s_lon0 >= 180.0f) s_lon0 -= 360.0f;
    if (s_lon0 < -180.0f) s_lon0 += 360.0f;
    drawGlobe();
    layoutLabels();
}

void destroy() {
    s_root = s_canvas = nullptr;
    for (int i = 0; i < NC; i++) s_city_lbl[i] = nullptr;
    if (s_buf)   { heap_caps_free(s_buf);   s_buf   = nullptr; }
    if (s_row)   { heap_caps_free(s_row);   s_row   = nullptr; }
    if (s_dcol)  { heap_caps_free(s_dcol);  s_dcol  = nullptr; }
    if (s_shade) { heap_caps_free(s_shade); s_shade = nullptr; }
}

}  // namespace face_world
