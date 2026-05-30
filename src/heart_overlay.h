// heart_overlay — full-screen heart bloom that pops on top of any face on
// heart-receive. Lives on lv_layer_top() so it overlays the active tile.
//
// Lifecycle:
//   create()  ← once, after LVGL is up
//   trigger() ← from the MQTT heart receive callback
//   update()  ← every loop iteration

#pragma once

namespace heart_overlay {

void create();
void trigger();
void update();
bool active();

}  // namespace heart_overlay
