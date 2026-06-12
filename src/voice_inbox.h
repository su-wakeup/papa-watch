// voice_inbox — the PAPA→Stanley voice mailbox. The bot keeps a rolling week of
// clips server-side; the watch fetches the list on demand and plays one only
// when tapped. Arrival is a retained MQTT {"cmd":"voice_new","ts":..} — we buzz
// once (de-duped by ts, persisted in NVS) and NEVER auto-play.
#pragma once
#include <stdint.h>

namespace voice_inbox {

struct Item { char id[40]; uint32_t ts; float secs; bool out; };  // out = a clip Stanley sent

void begin();                  // load the de-dup watermark from NVS
bool onPush(uint32_t ts);      // true if ts is newer than we've alerted on (→ buzz)
void fetch();                  // GET the inbox list (blocking HTTP; keep off the hot path)
int  count();
const Item& item(int i);
void play(int i);              // download + play clip i

}  // namespace voice_inbox
