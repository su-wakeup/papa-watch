// face_mech — mechanical analog watch face.
//   Background: minimal dark dial with 12 tick marks.
//   Hands: hour / minute / second sprites lifted from the LILYGO T-Encoder-Pro
//          SquareLine smartwatch demo (LVGL-8 → LVGL-9 ported via
//          scripts/port_lvgl8_image_to_9.py).
//   Update: lv_image_set_rotation each second; pivot anchored so the hands
//          appear to spin around the geometric center of the round screen.

#pragma once

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

namespace face_mech {

void create(lv_obj_t* parent);
void update();
void destroy();
void setUnread(int n);
void setStatus(bool wifi, bool mqtt);

}  // namespace face_mech
