// papa_quote — the latest "daily note from PAPA" (roadmap #5). Pushed from the
// bot as {"cmd":"quote","text":".."}; persisted in NVS so it survives the
// hourly heap-watchdog reboot. English only — the watch has no CJK font.

#pragma once

namespace papa_quote {

void        begin();              // load the stored note into RAM
void        set(const char* txt); // cache + persist
const char* text();               // "" if none received yet

}  // namespace papa_quote
