// papa_unread — a single "PAPA has a new message you haven't opened" flag,
// persisted in NVS. Set when a voice clip or a fresh daily note arrives,
// cleared when Stanley opens the PAPA app. The launcher reads it to badge the
// PAPA icon with an unread dot.
#pragma once

namespace papa_unread {

void begin();   // load the flag from NVS
void mark();    // a new PAPA message arrived → unread
void clear();   // the PAPA app was opened → read
bool any();     // is there an unread message? (launcher dot)

}  // namespace papa_unread
