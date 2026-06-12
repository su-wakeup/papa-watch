#include "papa_unread.h"
#include <Preferences.h>

namespace papa_unread {

static bool s_unread = false;

void begin() {
    Preferences p;
    p.begin("papa", true);              // shares the NVS namespace with papa_quote
    s_unread = p.getBool("unread", false);
    p.end();
}

static void persist() {
    Preferences p;
    p.begin("papa", false);
    p.putBool("unread", s_unread);
    p.end();
}

void mark()  { if (!s_unread) { s_unread = true;  persist(); } }
void clear() { if ( s_unread) { s_unread = false; persist(); } }
bool any()   { return s_unread; }

}  // namespace papa_unread
