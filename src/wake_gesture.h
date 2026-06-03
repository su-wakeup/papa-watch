// wake_gesture — BMI270-driven raise-to-look detection + idle power-down.
//
//   Sleep:  after 20s with no touch / button / raise event, cut CPU to 80MHz
//           and put the panel fully to sleep (pixels off).
//   Wake:   az dips LOW (pocket watch hanging, face sideways) then rises past
//           HIGH (lifted, face toward the eyes) → full speed + panel on.
//   Manual: any touch / button / incoming-heart also wakes via notifyActivity().

#pragma once
#include <stdint.h>

namespace wake_gesture {

void init();
void update();              // call once per main loop iteration
void notifyActivity();      // call on any touch / button event
bool isSleeping();

}  // namespace wake_gesture
