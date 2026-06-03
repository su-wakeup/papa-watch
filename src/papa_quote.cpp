#include "papa_quote.h"
#include <Preferences.h>
#include <string.h>

namespace papa_quote {

static constexpr size_t MAX_LEN = 240;
static char s_text[MAX_LEN + 1] = "";

void begin() {
    Preferences p;
    p.begin("papa", true);            // read-only
    String v = p.getString("quote", "");
    p.end();
    strncpy(s_text, v.c_str(), MAX_LEN);
    s_text[MAX_LEN] = 0;
}

void set(const char* txt) {
    if (!txt) txt = "";
    strncpy(s_text, txt, MAX_LEN);
    s_text[MAX_LEN] = 0;
    Preferences p;
    p.begin("papa", false);
    p.putString("quote", s_text);
    p.end();
}

const char* text() { return s_text; }

}  // namespace papa_quote
