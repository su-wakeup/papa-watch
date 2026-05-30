// app_sundial — replacement for app_compass on hardware without a
// magnetometer. Uses time + IP-derived lat/lon + solar position to tell the
// user the sun's apparent azimuth, then offers a "point watch top at sun,
// North is at the N-o'clock position" hint so the kid can get a coarse
// bearing the way old hikers did with a wristwatch.

#pragma once

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

namespace app_sundial {

void enter(lv_obj_t* parent);
void leave();
void tick();

}  // namespace app_sundial
