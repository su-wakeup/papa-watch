// face_lcd — Watch face B: 70s-style backlit LCD with DSEG7 digits and ghost segments.
// Palette: phosphor green on dark olive, dim "88:88" ghost behind live time so all
// unlit segments are visible (the classic Casio G-Shock look).

#pragma once
#include <stdint.h>

namespace face_lcd {

void create();
void update();          // refresh once per second
void destroy();         // tear down when switching faces
void showAlert(const char* text, uint32_t color_hex, uint32_t duration_ms);

}  // namespace face_lcd
