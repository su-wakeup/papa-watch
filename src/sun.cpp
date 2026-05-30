#include "sun.h"
#include <math.h>

namespace sun {

static constexpr double DEG = M_PI / 180.0;

static double norm360(double d) { d = fmod(d, 360.0); return d < 0 ? d + 360.0 : d; }

Pos positionAt(double lat_deg, double lon_deg, time_t utc) {
    // Days since J2000.0 (2000-01-01 12:00 UTC). Unix epoch is at Julian
    // Day 2440587.5; J2000 is JD 2451545.0. Difference in days = 10957.5.
    double n = (double)utc / 86400.0 - 10957.5;

    // Mean longitude and anomaly of the sun (degrees).
    double L = norm360(280.460 + 0.9856474 * n);
    double g = norm360(357.528 + 0.9856003 * n);

    // Ecliptic longitude (sun's apparent position on the ecliptic).
    double lambda = L + 1.915 * sin(g * DEG) + 0.020 * sin(2.0 * g * DEG);

    // Obliquity of the ecliptic.
    double eps = 23.439 - 0.0000004 * n;

    // Right ascension (atan2 keeps it in the correct quadrant) and declination.
    double sin_lambda = sin(lambda * DEG);
    double cos_lambda = cos(lambda * DEG);
    double cos_eps    = cos(eps    * DEG);
    double sin_eps    = sin(eps    * DEG);
    double ra   = atan2(cos_eps * sin_lambda, cos_lambda) / DEG;
    ra = norm360(ra);
    double decl = asin(sin_eps * sin_lambda) / DEG;

    // Greenwich Mean Sidereal Time in hours, then Local MST in degrees, then
    // hour angle.
    double gmst_h = fmod(18.697374558 + 24.06570982441908 * n, 24.0);
    if (gmst_h < 0) gmst_h += 24.0;
    double lmst  = norm360(gmst_h * 15.0 + lon_deg);
    double ha    = lmst - ra;

    // Horizontal coordinates.
    double sin_alt = sin(lat_deg * DEG) * sin(decl * DEG)
                   + cos(lat_deg * DEG) * cos(decl * DEG) * cos(ha * DEG);
    sin_alt = fmax(-1.0, fmin(1.0, sin_alt));
    double alt = asin(sin_alt) / DEG;

    double cos_alt = cos(alt * DEG);
    double cos_az  = (sin(decl * DEG) - sin(lat_deg * DEG) * sin_alt)
                   / (cos(lat_deg * DEG) * cos_alt);
    cos_az = fmax(-1.0, fmin(1.0, cos_az));
    double az = acos(cos_az) / DEG;
    if (sin(ha * DEG) > 0) az = 360.0 - az;        // sun is in the west half

    Pos p;
    p.azimuth   = (float)az;
    p.elevation = (float)alt;
    return p;
}

}  // namespace sun
