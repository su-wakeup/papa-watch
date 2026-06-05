#include "dad_loc.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

namespace dad_loc {

static bool     s_has = false;
static float    s_lat = 0, s_lon = 0;
static char     s_city[24] = {0};
static char     s_tz[40]   = {0};
static uint32_t s_version  = 0;

static constexpr const char* NVS_NS = "dadloc";

void load() {
    Preferences p; p.begin(NVS_NS, true);
    s_has = p.getBool("has", false);
    if (s_has) {
        s_lat = p.getFloat("lat", 0);
        s_lon = p.getFloat("lon", 0);
        String c = p.getString("city", "");
        String z = p.getString("tz", "");
        strncpy(s_city, c.c_str(), sizeof(s_city) - 1); s_city[sizeof(s_city)-1] = 0;
        strncpy(s_tz,   z.c_str(), sizeof(s_tz)   - 1); s_tz[sizeof(s_tz)-1]     = 0;
        s_version++;
    }
    p.end();
}

void set(const char* city, float lat, float lon, const char* tz) {
    if (!city || !city[0]) return;
    s_lat = lat; s_lon = lon;
    strncpy(s_city, city, sizeof(s_city) - 1); s_city[sizeof(s_city)-1] = 0;
    strncpy(s_tz, tz ? tz : "", sizeof(s_tz) - 1); s_tz[sizeof(s_tz)-1] = 0;
    s_has = true;
    s_version++;

    Preferences p; p.begin(NVS_NS, false);
    p.putBool ("has",  true);
    p.putFloat("lat",  s_lat);
    p.putFloat("lon",  s_lon);
    p.putString("city", s_city);
    p.putString("tz",   s_tz);
    p.end();
}

bool        has()     { return s_has; }
float       lat()     { return s_lat; }
float       lon()     { return s_lon; }
const char* city()    { return s_city; }
const char* tz()      { return s_tz[0] ? s_tz : "UTC0"; }
uint32_t    version() { return s_version; }

}  // namespace dad_loc
