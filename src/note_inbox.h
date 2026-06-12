// note_inbox — PAPA's recent text notes (the daily-quote history). The bot
// stores each note in R2; the watch fetches the rolling list on demand so the
// PAPA mailbox timeline can show past notes, not just the latest retained one.
#pragma once
#include <stdint.h>

namespace note_inbox {

struct Item { uint32_t ts; char text[241]; };

void fetch();                  // GET the recent notes (blocking HTTP; off the hot path)
int  count();
const Item& item(int i);

}  // namespace note_inbox
