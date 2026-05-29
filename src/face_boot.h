// face_boot — power-on greeting animation.
//   Layout: STANLEY (Pacifico 80, each letter a rainbow color, staggered fade-in)
//           tagline below in Pacifico 28
//   Timing: ~3.5s reveal+hold, ~0.8s fade out, then signals finished().
//
// Call create() once after LVGL is up. Call update() each loop iteration.
// When finished() returns true, destroy() and switch to the next screen.

#pragma once
#include <stdint.h>

namespace face_boot {

void create();
void update();
bool finished();
void destroy();

}  // namespace face_boot
