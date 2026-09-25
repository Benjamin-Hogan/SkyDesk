#include "geo.h"

#include <Arduino.h>
#include <math.h>

namespace geo {

namespace {
constexpr double D2R = M_PI / 180.0;
constexpr double R2D = 180.0 / M_PI;
constexpr double R_EFF = EARTH_R * 4.0 / 3.0;   // standard refraction

double norm360(double a) {
  a = fmod(a, 360.0);
  return a < 0 ? a + 360.0 : a;
}
}  // namespace

void distBearing(LatLon a, LatLon b, double &distM, double &bearingDeg) {
  const double p1 = a.lat * D2R, p2 = b.lat * D2R;
  const double dp = p2 - p1, dl = (b.lon - a.lon) * D2R;
  const double h = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
  distM = 2 * EARTH_R * atan2(sqrt(h), sqrt(1 - h));
  const double y = sin(dl) * cos(p2);
  const double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
  bearingDeg = norm360(atan2(y, x) * R2D);
}

double elevation(double distM, double altFt, double obsElevFt) {
  if (distM < 1.0) return 90.0;
  const double dh = (altFt - obsElevFt) * M_PER_FT;
  double el = atan2(dh - distM * distM / (2 * R_EFF), distM) * R2D;
  if (el < -10) el = -10;
  if (el > 90) el = 90;
  return el;
}

LatLon destination(LatLon p, double bearingDeg, double distM) {
  const double p1 = p.lat * D2R, l1 = p.lon * D2R, th = bearingDeg * D2R;
  const double d = distM / EARTH_R;
  const double p2 = asin(sin(p1) * cos(d) + cos(p1) * sin(d) * cos(th));
  const double l2 = l1 + atan2(sin(th) * sin(d) * cos(p1), cos(d) - sin(p1) * sin(p2));
  return {p2 * R2D, l2 * R2D};
}

void crossAlongTrack(LatLon a, LatLon b, LatLon p, double &xtM, double &atM) {
  double d13, t13, d12, t12;
  distBearing(a, p, d13, t13);
  distBearing(a, b, d12, t12);
  const double dA = d13 / EARTH_R;
  const double xt = asin(sin(dA) * sin((t13 - t12) * D2R));
  xtM = xt * EARTH_R;
  double c = cos(dA) / cos(xt);
  if (c > 1) c = 1;
  if (c < -1) c = -1;
  atM = acos(c) * EARTH_R;
  if (cos((t13 - t12) * D2R) < 0) atM = -atM;
}

const char *compass8(double az) {
  static const char *W[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  return W[(int)floor(norm360(az + 22.5) / 45.0) % 8];
}

const char *compass16(double az) {
  static const char *W[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                            "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
  return W[(int)floor(norm360(az + 11.25) / 22.5) % 16];
}

const char *relativeWords(double az, double viewUpDeg) {
  static const char *W[] = {"ahead", "ahead-right", "to your right", "behind-right",
                            "behind you", "behind-left", "to your left", "ahead-left"};
  return W[(int)floor(norm360(az - viewUpDeg + 22.5) / 45.0) % 8];
}

bool selfTest() {
  struct V { double lat, lon, alt, az, dnm, el; } v[] = {
    {33.4028, -111.7890, 10000, 0.0, 3.0, 25.6},
    {33.3528, -111.7290, 5000, 90.0, 3.01, 11.6},
    {33.3028, -111.8490, 35000, 225.1, 4.25, 52.6},
  };
  const LatLon obs{33.3528, -111.7890};
  bool ok = true;
  for (auto &t : v) {
    double d, az;
    distBearing(obs, {t.lat, t.lon}, d, az);
    const double el = elevation(d, t.alt, 1240);
    const bool pass = fabs(az - t.az) < 0.5 && fabs(d / M_PER_NM - t.dnm) < 0.05 && fabs(el - t.el) < 0.3;
    Serial.printf("[geo] az=%.1f d=%.2fnm el=%.1f  %s\n", az, d / M_PER_NM, el, pass ? "ok" : "FAIL");
    ok &= pass;
  }
  return ok;
}

}  // namespace geo
