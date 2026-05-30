// app_settings — system preferences (brightness / volume / vibrate),
// network controls (WiFi reconfig, OTA), dad-status override, and about.
// Persisted values land in the existing "papa-watch" NVS namespace.

#pragma once

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

namespace app_settings {

void enter(lv_obj_t* parent);
void leave();
void tick();

// Hardware-button shortcuts: A scrolls the list up, B scrolls down. Tap on
// any row uses touch to drive its inline control.
void scroll_up();
void scroll_down();

// Call once at boot — loads persisted brightness/volume/vibrate from NVS
// and applies them to M5.Display / M5.Speaker / hapticsEnabled().
void apply_on_boot();

// Other modules (haptics in main.cpp) read this to honour the user's choice
// to silence the buzzer.
bool vibrationEnabled();

}  // namespace app_settings
