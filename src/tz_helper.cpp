#include "tz_helper.h"
#include <stdlib.h>
#include <string.h>

namespace tz_helper {

// Up to 8 distinct zones in-flight at once. face_world peaks at 6 cities;
// face_lcd + face_mech add SON/DAD/PAPA which are duplicates of those.
// One extra slot for ad-hoc UTC/init calls. Linear scan is fine at this size.
struct Slot {
    const char* tz_str;            // strcmp key — nullptr = empty
    time_t      valid_until;       // epoch after which we must re-tzset
    int         offset_seconds;    // local - UTC, signed (PT is negative)
};
static constexpr int N_SLOTS = 8;
static Slot s_cache[N_SLOTS];

// Fall back to a real tzset+localtime for one zone, capture the resulting
// offset, and store it in `slot`. After this returns, `out` holds the
// correct local broken-down time.
static void refresh(Slot& slot, const char* tz, time_t epoch, struct tm* out) {
    setenv("TZ", tz, 1);
    tzset();
    localtime_r(&epoch, out);

    // newlib exposes the std-time offset via the global `timezone` (in
    // seconds, positive west of UTC, i.e. it's negated relative to ISO).
    // Add an hour if DST is currently active for this epoch.
    extern long timezone;
    int offset = -(int)timezone + (out->tm_isdst > 0 ? 3600 : 0);

    slot.tz_str         = tz;
    slot.valid_until    = epoch + 60;     // 60s — DST flips are coarse enough
    slot.offset_seconds = offset;
}

// Reconstruct struct tm from epoch + cached offset without touching tzset.
static void apply_cached(const Slot& slot, time_t epoch, struct tm* out) {
    time_t shifted = epoch + slot.offset_seconds;
    gmtime_r(&shifted, out);
    // gmtime_r reports UTC fields, but with `shifted = epoch + offset` they
    // numerically equal the local time fields. tm_isdst is unknown from
    // gmtime — leave as 0; nothing in the watch reads it post-conversion.
}

void localtime_in(const char* tz_posix, time_t epoch, struct tm* out) {
    // Cache lookup — pointer compare first (cheap), strcmp fallback so
    // different translation units with their own string literals still hit.
    for (int i = 0; i < N_SLOTS; i++) {
        Slot& s = s_cache[i];
        if (!s.tz_str) continue;
        if (epoch >= s.valid_until) continue;
        if (s.tz_str == tz_posix || strcmp(s.tz_str, tz_posix) == 0) {
            apply_cached(s, epoch, out);
            return;
        }
    }

    // Cache miss — find a slot to claim. Prefer empty; else evict the
    // oldest. Round-robin via slot pointer hash so two zones don't keep
    // kicking each other.
    int victim = 0;
    time_t oldest = s_cache[0].valid_until;
    for (int i = 0; i < N_SLOTS; i++) {
        if (!s_cache[i].tz_str) { victim = i; break; }
        if (s_cache[i].valid_until < oldest) {
            oldest = s_cache[i].valid_until;
            victim = i;
        }
    }
    refresh(s_cache[victim], tz_posix, epoch, out);
}

}  // namespace tz_helper
