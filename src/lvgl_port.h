// lvgl_port — bridge LVGL 9 to M5GFX (display) + M5.Touch (pointer input).
// Buffers live in PSRAM.  Tick advance + lv_timer_handler() pump are in tick().

#pragma once

namespace lvgl_port {

bool begin(int w = 468, int h = 468);
void tick();                  // call every loop iteration

}  // namespace lvgl_port
