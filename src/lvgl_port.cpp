#include "lvgl_port.h"
#include <M5Unified.h>
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <Arduino.h>

namespace lvgl_port {

static lv_display_t* s_disp  = nullptr;
static lv_indev_t*   s_indev = nullptr;
static uint8_t*      s_buf1  = nullptr;
static uint8_t*      s_buf2  = nullptr;

// ── display flush: blit LVGL's partial buffer to the panel via M5GFX ──
static void flush_cb(lv_display_t* d, const lv_area_t* area, uint8_t* px_map) {
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;
    M5.Display.startWrite();
    M5.Display.pushImage(area->x1, area->y1, w, h, (uint16_t*)px_map);
    M5.Display.endWrite();
    lv_display_flush_ready(d);
}

// ── pointer input from CST820B touch ──
static void touch_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    (void)indev;
    auto t = M5.Touch.getDetail();
    if (t.isPressed()) {
        data->point.x = t.x;
        data->point.y = t.y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state   = LV_INDEV_STATE_RELEASED;
    }
}

bool begin(int w, int h) {
    lv_init();

    // partial buffer: 80 rows × W × 2 bytes (RGB565), in PSRAM
    constexpr int LINES = 80;
    size_t buf_bytes = (size_t)w * LINES * sizeof(uint16_t);

    s_buf1 = (uint8_t*)heap_caps_aligned_alloc(16, buf_bytes, MALLOC_CAP_SPIRAM);
    s_buf2 = (uint8_t*)heap_caps_aligned_alloc(16, buf_bytes, MALLOC_CAP_SPIRAM);
    if (!s_buf1 || !s_buf2) {
        Serial.println("[lvgl] PSRAM alloc FAILED");
        return false;
    }

    s_disp = lv_display_create(w, h);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_disp, s_buf1, s_buf2, buf_bytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_disp, flush_cb);

    s_indev = lv_indev_create();
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_indev, touch_cb);

    Serial.printf("[lvgl] init %dx%d, buffers %u KB ea in PSRAM\n",
                  w, h, (unsigned)(buf_bytes / 1024));
    return true;
}

void tick() {
    static uint32_t last_ms = 0;
    uint32_t now = millis();
    if (last_ms == 0) last_ms = now;
    lv_tick_inc(now - last_ms);
    last_ms = now;
    lv_timer_handler();
}

}  // namespace lvgl_port
