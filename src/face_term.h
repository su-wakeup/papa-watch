// face_term — cyberpunk terminal watch face.
//   Black bg + phosphor green VT323 monospace + multi-line stat readout
//   + blinking cursor. Each line is "> cmd      value" two-column layout.

#pragma once

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

namespace face_term {

void create(lv_obj_t* parent);
void update();
void destroy();
void setUnread(int n);

}  // namespace face_term
