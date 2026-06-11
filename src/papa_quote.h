// papa_quote — the latest "daily note from PAPA" (roadmap #5). Pushed from the
// bot as {"cmd":"quote","text":".."}; persisted in NVS so it survives the
// hourly heap-watchdog reboot. English only — the watch has no CJK font.

#pragma once

#include <stdint.h>

namespace papa_quote {

void        begin();              // load the stored note into RAM
void        set(const char* txt); // cache + persist
const char* text();               // "" if none received yet
uint32_t    ts();                 // arrival epoch of the current note (0 if none/pre-NTP)

}  // namespace papa_quote
