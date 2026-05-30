// sun — solar position calculator. Standard NOAA "low-precision" formulas
// from the public-domain NOAA Solar Calculator algorithm description. Good to
// ~0.01° which is plenty when we only want to tell a kid which way is north.

#pragma once
#include <time.h>

namespace sun {

struct Pos {
    float azimuth;     // degrees clockwise from true north (0..360)
    float elevation;   // degrees above horizon (-90..+90)
};

// Compute apparent solar position at given location and UTC time.
Pos positionAt(double lat_deg, double lon_deg, time_t utc);

}  // namespace sun
