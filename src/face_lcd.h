// face_lcd — Watch face B: 70s-style backlit LCD with DSEG7 digits and ghost segments.
// Palette: phosphor green on dark olive, dim "88:88" ghost behind live time so all
// unlit segments are visible (the classic Casio G-Shock look).

#pragma once
#include <stdint.h>

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

namespace face_lcd {

void create(lv_obj_t* parent);   // build widgets as children of `parent`
void update();                   // refresh once per second
void destroy();                  // tear down (caller owns parent)
void showAlert(const char* text, uint32_t color_hex, uint32_t duration_ms);
void setStatus(bool wifi_up, bool mqtt_up, int unread_count);

}  // namespace face_lcd
