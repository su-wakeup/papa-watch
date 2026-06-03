// face_world — Watch sub-tile #4. A spinnable globe (orthographic, viewed from
// above the equator) so Stanley can rotate the Earth with the A/B buttons, see
// where each city is, and read its local time — a world clock that also teaches
// a little geography. Phase 1: graticule + city dots + times. Phase 2: real
// continent outlines + day/night terminator.

#pragma once

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

namespace face_world {

void create(lv_obj_t* parent);
void update();
void destroy();
void rotate(int dir);    // -1 = spin west, +1 = spin east (A/B buttons)

}  // namespace face_world
