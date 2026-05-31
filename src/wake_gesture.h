// wake_gesture — BMI270-driven wrist-raise detection + auto-dim.
//
//   Sleep:  after 20s with no touch / button / wrist-raise event, dim the
//           display to ~20% of the user's chosen brightness.
//   Wake:   accel.z crossing low→high (watch face was sideways/down, now
//           tilted up toward the eyes) → restore full brightness.
//   Manual: any touch/button event also wakes via notifyActivity().

#pragma once
#include <stdint.h>

namespace wake_gesture {

void init();
void update();              // call once per main loop iteration
void notifyActivity();      // call on any touch / button event
bool isSleeping();

}  // namespace wake_gesture
