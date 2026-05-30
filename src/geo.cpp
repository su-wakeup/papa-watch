#include "geo.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <string.h>

namespace geo {

static constexpr const char* NVS_NS  = "papa-watch";
static constexpr uint32_t MAX_AGE_SEC = 24UL * 60 * 60;    // 24h cache

// One-shot fetch from ip-api.com. Free, no key, ~45 req/min — plenty for once
// a day. We ask for only the 4 fields we need to keep the payload tiny.
static bool fetchFromIp(Location* out) {
    if (WiFi.status() != WL_CONNECTED) return false;
    HTTPClient http;
    if (!http.begin("http://ip-api.com/json/?fields=status,city,lat,lon")) return false;
    int code = http.GET();
    if (code != HTTP_CODE_OK) { http.end(); return false; }
    String body = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, body)) return false;
    const char* status = doc["status"] | "";
    if (strcmp(status, "success") != 0) return false;
    out->lat = doc["lat"].as<float>();
    out->lon = doc["lon"].as<float>();
    const char* city = doc["city"] | "Unknown";
    strncpy(out->city, city, sizeof(out->city) - 1);
    out->city[sizeof(out->city) - 1] = 0;
    return true;
}

static bool loadCached(Location* out) {
    Preferences p;
    p.begin(NVS_NS, true);
    bool has = p.isKey("geo_lat") && p.isKey("geo_ts");
    if (has) {
        uint32_t ts = p.getULong("geo_ts", 0);
        uint32_t now_sec = (uint32_t)(time(nullptr));
        if (now_sec - ts > MAX_AGE_SEC) {
            has = false;          // stale
        } else {
            out->lat = p.getFloat("geo_lat", 0);
            out->lon = p.getFloat("geo_lon", 0);
            String city = p.getString("geo_city", "Unknown");
            strncpy(out->city, city.c_str(), sizeof(out->city) - 1);
            out->city[sizeof(out->city) - 1] = 0;
        }
    }
    p.end();
    return has;
}

static void saveCached(const Location* in) {
    Preferences p;
    p.begin(NVS_NS, false);
    p.putFloat("geo_lat", in->lat);
    p.putFloat("geo_lon", in->lon);
    p.putString("geo_city", in->city);
    p.putULong("geo_ts", (uint32_t)time(nullptr));
    p.end();
}

bool getLocation(Location* out) {
    if (loadCached(out)) return true;
    if (!fetchFromIp(out)) return false;
    saveCached(out);
    return true;
}

bool refreshLocation(Location* out) {
    if (!fetchFromIp(out)) return false;
    saveCached(out);
    return true;
}

}  // namespace geo
