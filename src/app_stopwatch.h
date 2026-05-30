// app_stopwatch — high-precision lap timer. State persists across leave/enter
// cycles so the timer keeps running in the background even when the user
// pops back to the launcher (Apple Watch-style behaviour).

#pragma once

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

namespace app_stopwatch {

void enter(lv_obj_t* parent);
void leave();
void tick();

// Hardware-button shortcuts so the watch behaves like a real physical
// stopwatch: BtnA = START/STOP toggle, BtnB = LAP (running) / RESET (paused).
// Same semantics as the on-screen touch buttons.
void press_run();
void press_aux();

}  // namespace app_stopwatch
