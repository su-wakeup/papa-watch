// dad_loc — where PAPA actually is right now, pushed from Telegram (/where)
// over MQTT and persisted to NVS so the globe shows it immediately on boot.
// Distinct from geo:: (this watch's own location) and from TZ_DAD (dad's fixed
// home timezone on the heart face).

#pragma once
#include <stdint.h>

namespace dad_loc {

void        load();          // restore last-known location from NVS at boot
bool        has();           // true once a real /where has been received
void        set(const char* city, float lat, float lon, const char* tz);

float       lat();
float       lon();
const char* city();          // display name, e.g. "Shenzhen"
const char* tz();            // POSIX TZ for that city's local time

uint32_t    version();       // bumps on each set — lets the globe redraw promptly

}  // namespace dad_loc
