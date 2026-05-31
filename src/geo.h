// geo — coarse IP-based geolocation, cached in NVS so we only call out
// once. ip-api.com gives ~city-level accuracy for free; that's plenty for a
// sundial-style compass that only needs lat/lon to ~1° precision.

#pragma once

namespace geo {

struct Location {
    float lat;            // degrees north (positive = N)
    float lon;            // degrees east  (positive = E)
    char  city[24];
};

// Try cached NVS value first; if none / stale, hit ip-api.com over WiFi.
// Returns false when WiFi is down and no cache exists.
bool getLocation(Location* out);

// Force a re-fetch (drops cache, queries fresh). Useful from Settings when
// the user crosses time zones.
bool refreshLocation(Location* out);

// Manual override for VPN / wrong-IP-geo cases. Pass 0 to fall back to IP
// auto-detection; 1..PRESETS_N selects from the preset city table. Stored
// in NVS so it survives reboot.
struct Preset { const char* name; float lat; float lon; };
extern const Preset PRESETS[];
extern const int    PRESETS_N;

int  getOverrideIdx();        // 0 = auto, 1..N = preset index
void setOverrideIdx(int idx); // persists to NVS immediately

}  // namespace geo
