// face_world — Watch sub-tile #4. Six-city world clock list so Stanley can
// glance and see what time it is back home (Beijing) and at any of the
// other major checkpoints — useful for "is it OK to call Mom right now?".

#pragma once

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

namespace face_world {

void create(lv_obj_t* parent);
void update();
void destroy();

}  // namespace face_world
