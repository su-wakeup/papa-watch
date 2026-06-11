#include "papa_quote.h"
#include <Preferences.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

namespace papa_quote {

static constexpr size_t MAX_LEN = 240;
static char     s_text[MAX_LEN + 1] = "";
static uint32_t s_ts = 0;             // arrival time of the current note (epoch)

void begin() {
    Preferences p;
    p.begin("papa", true);            // read-only
    String v = p.getString("quote", "");
    s_ts = p.getUInt("quote_ts", 0);
    p.end();
    strncpy(s_text, v.c_str(), MAX_LEN);
    s_text[MAX_LEN] = 0;
}

void set(const char* txt) {
    if (!txt) txt = "";
    if (strncmp(txt, s_text, MAX_LEN) == 0) return;   // unchanged (retained re-delivery) — keep ts
    strncpy(s_text, txt, MAX_LEN);
    s_text[MAX_LEN] = 0;
    time_t now = time(nullptr);
    s_ts = (now > 1000000000) ? (uint32_t)now : 0;    // 0 if clock not yet NTP-synced
    Preferences p;
    p.begin("papa", false);
    p.putString("quote", s_text);
    p.putUInt("quote_ts", s_ts);
    p.end();
}

const char* text() { return s_text; }
uint32_t    ts()   { return s_ts; }

}  // namespace papa_quote
