// dad_status — what dad's up to, by his Beijing-hour clock plus an optional
// override sent from his Telegram bot (`/status free|busy|away|thinking|auto`).
// Override expires after a fixed window so the watch quietly returns to the
// auto/timezone reading; "auto" command clears the override immediately.

#pragma once
#include <stdint.h>

namespace dad_status {

enum State : int8_t {
    NONE        = -1,           // sentinel for "no override"
    SLEEPING    =  0,
    WORKING     =  1,
    AWAKE       =  2,
    FREE        =  3,
    BUSY        =  4,
    AWAY        =  5,
    THINKING    =  6,
};

State        current(int beijing_hour);     // auto + override merged
const char*  label(State s);
uint32_t     color(State s);
bool         overrideActive();
void         setOverride(const char* name, uint32_t hours);
void         clearOverride();

}  // namespace dad_status
