// geo.h - sky geometry (docs/05-sky-geometry.md). Pure math, no Arduino deps
// beyond <math.h>, so it can be unit tested on the host.
#pragma once

#include <stdint.h>

namespace geo {

constexpr double EARTH_R = 6371008.8;     // metres
constexpr double M_PER_NM = 1852.0;
constexpr double M_PER_MI = 1609.344;
constexpr double M_PER_FT = 0.3048;

struct LatLon { double lat, lon; };

// Great-circle distance (m) and initial bearing (deg 0..360) from a to b.
void distBearing(LatLon a, LatLon b, double &distM, double &bearingDeg);

// Elevation angle (deg, -10..90) of an aircraft at ground distance distM,
// altitude altFt (MSL), seen from an observer at obsElevFt (MSL).
double elevation(double distM, double altFt, double obsElevFt);

// Point reached from p travelling distM on bearingDeg.
LatLon destination(LatLon p, double bearingDeg, double distM);

// Cross-track (m, signed) and along-track (m) of p relative to great circle a->b.
void crossAlongTrack(LatLon a, LatLon b, LatLon p, double &xtM, double &atM);

// 8-point / 16-point compass words.
const char *compass8(double az);
const char *compass16(double az);

// "ahead", "behind you", ... relative to the direction the user faces.
const char *relativeWords(double az, double viewUpDeg);

// Run the docs test vectors; prints to Serial, returns false on mismatch.
bool selfTest();

}  // namespace geo
