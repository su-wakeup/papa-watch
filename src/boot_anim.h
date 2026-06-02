// boot_anim — the PaPaWatch boot ceremony. 40 PNG frames on LittleFS, blitted
// straight to the panel by M5GFX *before* LVGL starts, so it never touches
// LVGL's 96 KB memory pool (a full frame would OOM it). See data/boot/.
#pragma once

namespace boot_anim {
// Mount LittleFS and play the frame sequence, leaving the final frame on
// screen. Blocking (~2 s). Call once in setup() right after M5.begin().
void play();
}
