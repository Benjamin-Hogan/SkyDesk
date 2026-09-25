#include "map_model.h"
#include "geo.h"

#include <math.h>

namespace {
Trail g_trails[MAP_TRAILS];
constexpr uint32_t TRAIL_EXPIRE_MS = 60000;
}  // namespace

void mapProject(double lat, double lon, float pxPerNm, float &x, float &y) {
  const double coslat = cos(OBS_LAT * DEG_TO_RAD);
  x = MAP_CX + (float)((lon - OBS_LON) * 60.0 * coslat * pxPerNm);
  y = MAP_CY - (float)((lat - OBS_LAT) * 60.0 * pxPerNm);
}

const Trail *trailFor(const char *hex) {
  for (const auto &tr : g_trails)
    if (tr.used && strcmp(tr.hex, hex) == 0) return &tr;
  return nullptr;
}

void trailsUpdate(const Traffic &t, uint32_t now) {
  for (uint8_t i = 0; i < t.n; i++) {
    const Aircraft &a = t.ac[i];
    if (a.onGround) continue;
    Trail *tr = const_cast<Trail *>(trailFor(a.hex));
    if (!tr) {
      Trail *victim = nullptr;
      for (auto &c : g_trails) {
        if (!c.used) { victim = &c; break; }
        if (!victim || c.seenMs < victim->seenMs) victim = &c;   // oldest
      }
      tr = victim;
      memset(tr, 0, sizeof(*tr));
      strncpy(tr->hex, a.hex, sizeof(tr->hex) - 1);
      tr->used = true;
      tr->head = MAP_TRAIL_N - 1;
    }
    tr->seenMs = now;
    // Only append a genuinely new position (the feed repeats stale ones).
    if (tr->n && fabsf(tr->lat[tr->head] - (float)a.lat) < 1e-5f && fabsf(tr->lon[tr->head] - (float)a.lon) < 1e-5f)
      continue;
    tr->head = (tr->head + 1) % MAP_TRAIL_N;
    tr->lat[tr->head] = (float)a.lat;
    tr->lon[tr->head] = (float)a.lon;
    if (tr->n < MAP_TRAIL_N) tr->n++;
  }
  for (auto &tr : g_trails)
    if (tr.used && now - tr.seenMs > TRAIL_EXPIRE_MS) tr.used = false;
}

bool mapQualifies(double distNm, double elDeg, double altFt) {
  return distNm <= ENTER_RADIUS_NM && elDeg >= ENTER_MIN_ELEV_DEG && altFt - OBS_ELEV_FT >= MIN_AGL_FT;
}

uint8_t mapWillPopSecs(const Aircraft &a) {
  if (a.onGround || a.track < 0 || a.gsKt < 30 || mapQualifies(a.distNm, a.elDeg, a.altFt)) return 0;
  for (uint8_t s = 15; s <= 60; s += 15) {
    const geo::LatLon f = geo::destination({a.lat, a.lon}, a.track, a.gsKt * 0.514444 * s);
    const double alt = a.altFt + a.vRateFpm * (s / 60.0);
    double d, brg;
    geo::distBearing({OBS_LAT, OBS_LON}, f, d, brg);
    if (mapQualifies(d / geo::M_PER_NM, geo::elevation(d, alt, OBS_ELEV_FT), alt)) return s;
  }
  return 0;
}

int mapHitTest(const Traffic &t, float pxPerNm, int16_t x, int16_t y, int16_t radiusPx) {
  int best = -1;
  float bestD = radiusPx;
  for (uint8_t i = 0; i < t.n; i++) {
    if (t.ac[i].onGround) continue;
    float ax, ay;
    mapProject(t.ac[i].lat, t.ac[i].lon, pxPerNm, ax, ay);
    const float d = hypotf(ax - x, ay - y);
    if (d <= bestD) { bestD = d; best = i; }
  }
  return best;
}
