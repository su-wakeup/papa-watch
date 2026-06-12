// ota_ui — full-screen progress overlay shown during an OTA flash so the long
// synchronous Update.writeStream() reads as "updating" instead of a frozen
// face. Lives on lv_layer_top(); both OTA call sites run on the LVGL/main
// thread, so the synchronous lv_refr_now() inside is safe.
#pragma once

namespace ota_ui {

void begin();            // raise the "UPDATING / DO NOT POWER OFF" overlay
void progress(int pct);  // update bar + %, force a redraw (throttled to %-changes)
void dismiss();          // tear it down (OTA failed → underlying app returns)

}  // namespace ota_ui
