#include "face_mech.h"
#include <Arduino.h>
#include <lvgl.h>
#include <time.h>
#include <stdlib.h>
#include <math.h>

// Hand sprites ported from LILYGO T-Encoder-Pro SquareLine demo
LV_IMAGE_DECLARE(ui_img_clockwise_hour_png);
LV_IMAGE_DECLARE(ui_img_clockwise_min_png);
LV_IMAGE_DECLARE(ui_img_clockwise_sec_png);

namespace face_mech {

static constexpr const char* TZ_SON = "PST8PDT,M3.2.0,M11.1.0";
static constexpr int CX = 234;        // round screen geometric center
static constexpr int CY = 234;

// Pivot points inside each hand image (where the rotation knob sits).
// SquareLine's hands point "up" by default; the pivot is along the centerline
// of the hand, slightly above its bottom edge.
//   image (w × h)        pivot (x, y inside image)
//   hour  18 × 98        ( 9, 78)
//   min   18 × 157       ( 9, 125)
//   sec   31 × 180       (15, 150)
static constexpr int HOUR_W = 18, HOUR_H = 98;
static constexpr int HOUR_PX = 9,  HOUR_PY = 78;
static constexpr int MIN_W  = 18, MIN_H  = 157;
static constexpr int MIN_PX  = 9,  MIN_PY  = 125;
static constexpr int SEC_W  = 31, SEC_H  = 180;
static constexpr int SEC_PX  = 15, SEC_PY  = 150;

static lv_obj_t* s_root  = nullptr;
static lv_obj_t* s_hour  = nullptr;
static lv_obj_t* s_min   = nullptr;
static lv_obj_t* s_sec   = nullptr;
static lv_obj_t* s_pin   = nullptr;   // center cap

static lv_obj_t* makeHand(lv_obj_t* parent, const lv_image_dsc_t* src,
                          int pivot_x, int pivot_y) {
    lv_obj_t* img = lv_image_create(parent);
    lv_image_set_src(img, src);
    lv_image_set_pivot(img, pivot_x, pivot_y);
    // Position the image's top-left so that its pivot lands on (CX, CY).
    lv_obj_set_pos(img, CX - pivot_x, CY - pivot_y);
    return img;
}

void create(lv_obj_t* parent) {
    s_root = parent;
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0x050505), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);

    // ── tick marks (12 around the dial) ──
    // Draw as small lines via lv_line; major ticks at 12/3/6/9 are longer.
    for (int i = 0; i < 12; i++) {
        // 0° at 12 o'clock; LVGL's coordinate system has y growing downward,
        // so subtract pi/2 to put 0 at top.
        float a = (i * 30.0f - 90.0f) * 3.14159265f / 180.0f;
        bool major = (i % 3 == 0);
        int r_outer = 225;
        int r_inner = major ? 196 : 208;
        int x1 = CX + (int)(cosf(a) * r_outer);
        int y1 = CY + (int)(sinf(a) * r_outer);
        int x2 = CX + (int)(cosf(a) * r_inner);
        int y2 = CY + (int)(sinf(a) * r_inner);
        static lv_point_precise_t pts[24][2];     // keep array static so LVGL can read at render time
        pts[i][0] = {(lv_value_precise_t)x1, (lv_value_precise_t)y1};
        pts[i][1] = {(lv_value_precise_t)x2, (lv_value_precise_t)y2};
        lv_obj_t* line = lv_line_create(s_root);
        lv_line_set_points(line, pts[i], 2);
        lv_obj_set_style_line_color(line, lv_color_hex(major ? 0xC8B070 : 0x806C40), 0);
        lv_obj_set_style_line_width(line, major ? 4 : 2, 0);
        lv_obj_set_style_line_rounded(line, true, 0);
    }

    // ── hands (back-to-front: hour → minute → second) ──
    s_hour = makeHand(s_root, &ui_img_clockwise_hour_png, HOUR_PX, HOUR_PY);
    s_min  = makeHand(s_root, &ui_img_clockwise_min_png,  MIN_PX,  MIN_PY);
    s_sec  = makeHand(s_root, &ui_img_clockwise_sec_png,  SEC_PX,  SEC_PY);

    // ── center cap ──
    s_pin = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_pin);
    lv_obj_set_size(s_pin, 14, 14);
    lv_obj_set_style_radius(s_pin, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_pin, lv_color_hex(0xE6C46D), 0);
    lv_obj_set_style_bg_opa(s_pin, LV_OPA_COVER, 0);
    lv_obj_align(s_pin, LV_ALIGN_CENTER, 0, 0);

    update();
}

void update() {
    if (!s_hour) return;

    setenv("TZ", TZ_SON, 1);
    tzset();
    time_t now = time(nullptr);
    struct tm t; localtime_r(&now, &t);

    int h = t.tm_hour % 12;
    int m = t.tm_min;
    int s = t.tm_sec;

    // LVGL rotation is in 0.1° units (3600 = 360°)
    int hour_angle = (h * 300) + (m * 5);      // each hr 30° + half-degree per min
    int min_angle  = m * 60;                    // each min 6°
    int sec_angle  = s * 60;

    lv_image_set_rotation(s_hour, hour_angle);
    lv_image_set_rotation(s_min,  min_angle);
    lv_image_set_rotation(s_sec,  sec_angle);
}

void destroy() {
    s_root = s_hour = s_min = s_sec = s_pin = nullptr;
}

}  // namespace face_mech
