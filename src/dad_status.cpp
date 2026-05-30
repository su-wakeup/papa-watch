#include "dad_status.h"
#include <Arduino.h>
#include <string.h>
#include <strings.h>

namespace dad_status {

static State    s_override          = NONE;
static uint32_t s_override_until_ms = 0;

static State autoByHour(int h) {
    if (h >= 23 || h < 7)  return SLEEPING;
    if (h >= 9  && h < 18) return WORKING;
    return AWAKE;
}

State current(int beijing_hour) {
    if (s_override != NONE && millis() < s_override_until_ms) {
        return s_override;
    }
    if (s_override != NONE && millis() >= s_override_until_ms) {
        // window closed; quietly fall back to auto
        s_override = NONE;
    }
    return autoByHour(beijing_hour);
}

const char* label(State s) {
    switch (s) {
        case SLEEPING: return "SLEEPING";
        case WORKING:  return "WORKING";
        case AWAKE:    return "AWAKE";
        case FREE:     return "FREE";
        case BUSY:     return "BUSY";
        case AWAY:     return "AWAY";
        case THINKING: return "THINKING";
        default:       return "?";
    }
}

uint32_t color(State s) {
    switch (s) {
        case SLEEPING: return 0x4A8AC8;     // soft blue
        case WORKING:  return 0xFFB000;     // amber
        case AWAKE:    return 0x39FF14;     // electric green
        case FREE:     return 0x39FF14;
        case BUSY:     return 0xFF4040;     // red
        case AWAY:     return 0x808080;
        case THINKING: return 0xFF1493;     // magenta
        default:       return 0xFFFFFF;
    }
}

bool overrideActive() {
    return s_override != NONE && millis() < s_override_until_ms;
}

void setOverride(const char* name, uint32_t hours) {
    if (!name) return;
    State s = NONE;
    if      (!strcasecmp(name, "free"))     s = FREE;
    else if (!strcasecmp(name, "busy"))     s = BUSY;
    else if (!strcasecmp(name, "away"))     s = AWAY;
    else if (!strcasecmp(name, "thinking")) s = THINKING;
    else if (!strcasecmp(name, "auto"))   { clearOverride(); return; }
    if (s == NONE) return;
    s_override          = s;
    s_override_until_ms = millis() + hours * 3600U * 1000U;
    Serial.printf("[status] override → %s (%u h)\n", name, (unsigned)hours);
}

void clearOverride() {
    s_override          = NONE;
    s_override_until_ms = 0;
    Serial.println("[status] override cleared, back to auto");
}

}  // namespace dad_status
