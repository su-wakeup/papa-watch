// face_lvgl — Phase 1 placeholder watch face built in LVGL.
//   Goal of this file: prove LVGL renders correctly on the 468×468 round panel.
//   Visual is intentionally basic (Montserrat default, no DSEG, no heart yet).
//   Real face designs (70s LCD / mechanical / cyber terminal) come in later tasks.

#pragma once

namespace face_lvgl {

void create();    // build the LVGL screen + widgets
void update();    // refresh time labels (call ~1/sec)

}  // namespace face_lvgl
