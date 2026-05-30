// steps — naive BMI270 pedometer. Filtered accel magnitude → peak detect
// with refractory period. Persisted per-day in NVS, auto-resets at midnight
// (local TZ assumed to be Stanley's, since this is his watch).

#pragma once
#include <stdint.h>

namespace steps {

void   init();          // load today's count from NVS
void   update();        // call every loop()
int    today();         // current step count
void   resetIfNewDay(); // call once per second; rolls over the day key

}  // namespace steps
