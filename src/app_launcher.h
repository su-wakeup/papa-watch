// app_launcher — carousel home: a big Phosphor-Fill icon in the middle, two
// smaller siblings poking in from the edges, app name underneath. BtnA cycles
// left, BtnB right, BtnA+BtnB returns from any app back here.

#pragma once

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

namespace app_launcher {

void enter(lv_obj_t* parent);
void leave();
void tick();

// Wheel navigation — driven by the hardware buttons from main.cpp.
void rotate_left();           // BtnA
void rotate_right();          // BtnB
void activate_center();       // tap big icon (also called on no-touch fallback)

}  // namespace app_launcher
