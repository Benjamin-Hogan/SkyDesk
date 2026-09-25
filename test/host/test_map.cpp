// Host tests for map_model.cpp (projection, trails, hit-testing).
#include "geo.h"
#include "map_model.h"

extern int g_fail;
#define CHECK(cond, msg)                                              \
  do {                                                                \
    if (!(cond)) { std::printf("  FAIL: %s (line %d)\n", msg, __LINE__); g_fail++; } \
    else std::printf("  ok:   %s\n", msg);                            \
  } while (0)

static Aircraft at(const char *hex, double lat, double lon, bool ground = false) {
  Aircraft a{};
  strncpy(a.hex, hex, sizeof(a.hex) - 1);
  a.lat = lat; a.lon = lon; a.onGround = ground; a.altFt = ground ? 0 : 8000;
  return a;
}

void testMap() {
  std::printf("plane map model (docs/08)\n");
  float x, y;
  mapProject(OBS_LAT, OBS_LON, 10, x, y);
  CHECK(fabsf(x - MAP_CX) < 0.01f && fabsf(y - MAP_CY) < 0.01f, "observer projects to the map centre");

  // 5 nm due north / east at 10 px/nm -> 50 px (equirectangular, < 1 px error)
  const geo::LatLon n = geo::destination({OBS_LAT, OBS_LON}, 0, 5 * geo::M_PER_NM);
  const geo::LatLon e = geo::destination({OBS_LAT, OBS_LON}, 90, 5 * geo::M_PER_NM);
  mapProject(n.lat, n.lon, 10, x, y);
  CHECK(fabsf(x - MAP_CX) < 0.5f && fabsf((MAP_CY - y) - 50) < 0.5f, "5 nm north -> 50 px up");
  mapProject(e.lat, e.lon, 10, x, y);
  CHECK(fabsf((x - MAP_CX) - 50) < 0.5f && fabsf(y - MAP_CY) < 0.5f, "5 nm east -> 50 px right");

  // Trails: new points only, ground aircraft ignored, expiry after 60 s.
  Traffic t{};
  t.n = 2;
  t.ac[0] = at("aaa001", 33.40, -111.80);
  t.ac[1] = at("ggg001", 33.43, -112.00, true);
  trailsUpdate(t, 1000);
  trailsUpdate(t, 3000);                          // same position repeated by the feed
  t.ac[0].lat += 0.01;
  trailsUpdate(t, 5000);
  const Trail *tr = trailFor("aaa001");
  CHECK(tr && tr->n == 2, "trail keeps only distinct positions");
  CHECK(trailFor("ggg001") == nullptr, "no trail for aircraft on the ground");
  for (int k = 0; k < 12; k++) { t.ac[0].lat += 0.01; trailsUpdate(t, 7000 + k * 2000); }
  CHECK(trailFor("aaa001")->n == MAP_TRAIL_N, "trail capped at MAP_TRAIL_N points");
  t.n = 0;
  trailsUpdate(t, 100000);
  CHECK(trailFor("aaa001") == nullptr, "trail expires 60 s after the hex disappears");

  // Hit test: tap next to a plane selects it; ground aircraft can't be tapped.
  t.n = 2;
  t.ac[0] = at("aaa001", n.lat, n.lon);
  t.ac[1] = at("ggg001", OBS_LAT, OBS_LON, true);
  CHECK(mapHitTest(t, 10, MAP_CX + 6, MAP_CY - 46, 18) == 0, "tap within 18 px hits the plane");
  CHECK(mapHitTest(t, 10, MAP_CX, MAP_CY, 18) == -1, "ground aircraft are not tappable");

  // "Will pop": a 250 kt jet 5,000 ft above you, crossing 1.5 nm to the side. It
  // qualifies only between ~+20 s and ~+45 s: a +60 s-only check would miss it.
  const geo::LatLon abeam = geo::destination({OBS_LAT, OBS_LON}, 90, 1.5 * geo::M_PER_NM);
  const geo::LatLon start = geo::destination(abeam, 180, 2.0 * geo::M_PER_NM);
  Aircraft j = at("jet250", start.lat, start.lon);
  j.altFt = OBS_ELEV_FT + 5000; j.track = 0; j.gsKt = 250;
  double dd, bb;
  geo::distBearing({OBS_LAT, OBS_LON}, {j.lat, j.lon}, dd, bb);
  j.distNm = dd / geo::M_PER_NM; j.elDeg = geo::elevation(dd, j.altFt, OBS_ELEV_FT);
  CHECK(!mapQualifies(j.distNm, j.elDeg, j.altFt), "crossing jet does not qualify yet");
  {   // prove the point: at +60 s alone it does NOT qualify
    const geo::LatLon f = geo::destination({j.lat, j.lon}, 0, 250 * 0.514444 * 60);
    double d60, b60;
    geo::distBearing({OBS_LAT, OBS_LON}, f, d60, b60);
    CHECK(!mapQualifies(d60 / geo::M_PER_NM, geo::elevation(d60, j.altFt, OBS_ELEV_FT), j.altFt),
          "a +60 s-only check would miss the crossing jet");
  }
  const uint8_t secs = mapWillPopSecs(j);
  CHECK(secs > 0 && secs < 60, "crossing jet is flagged 'will pop' by the path samples (not only +60 s)");
  j.track = 180;   // same jet flying away
  CHECK(mapWillPopSecs(j) == 0, "a jet flying away is not flagged");
}
