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

}  // namespace geo
