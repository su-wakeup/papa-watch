// tz_helper — drop-in replacement for setenv("TZ",...)+tzset()+localtime_r().
//
// ESP32 newlib leaks ~40 B every tzset() call. The watch faces hit it 2-11
// times per second, which burns ~5 KB / 30 s and eventually triggers the
// heap watchdog (~6 h to reboot). This helper caches the (TZ string →
// UTC-offset-seconds) result for 60 s per zone, so we tzset at most once
// per minute per zone instead of every tick.
//
// Replace every call of the form
//     setenv("TZ", "PST8PDT,...", 1); tzset();
//     struct tm t; localtime_r(&epoch, &t);
// with
//     struct tm t; tz_helper::localtime_in("PST8PDT,...", epoch, &t);

#pragma once

#include <time.h>

namespace tz_helper {

void localtime_in(const char* tz_posix, time_t epoch, struct tm* out);

}  // namespace tz_helper
