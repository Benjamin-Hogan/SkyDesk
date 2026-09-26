// Host tests for the Sky Trails recorder format: trails_log.cpp (docs/13-sky-trails.md).
#include <cmath>
#include <cstdio>
#include <cstring>

#include "config.h"
#include "trails_log.h"

extern int g_fail;
#define CHECK(cond, msg)                                                             \
  do {                                                                               \
    if (!(cond)) { std::printf("  FAIL: %s (line %d)\n", msg, __LINE__); g_fail++; } \
    else std::printf("  ok:   %s\n", msg);                                          \
  } while (0)

namespace {
Aircraft fix(const char *hex, double lat, double lon, int alt, float seen = 1, bool ground = false) {
  Aircraft a{};
  strncpy(a.hex, hex, sizeof(a.hex) - 1);
  a.lat = lat;
  a.lon = lon;
  a.altFt = alt;
  a.seenPos = seen;
  a.onGround = ground;
  return a;
}
}  // namespace

void testTrails() {
  std::printf("trails: honesty filter, encode/decode (docs/13)\n");
  const double bLat = 33.3528, bLon = -111.7890;   // the public default build point
  CHECK(trailsKeep(fix("a1b2c3", 33.4, -111.9, 5000)), "a fresh airborne ADS-B fix is kept");
  CHECK(!trailsKeep(fix("~2a0f1", 33.4, -111.9, 5000)), "TIS-B (~ hex) is never recorded");
  CHECK(!trailsKeep(fix("a1b2c3", 33.4, -111.9, 1200, 1, true)), "ground traffic skipped");
  CHECK(!trailsKeep(fix("a1b2c3", 33.4, -111.9, 5000, MAX_SEEN_POS_S + 1)), "a stale position skipped");

  Aircraft ac[4] = {fix("a1b2c3", 33.4342, -112.0116, 3400),   // PHX final, ~12 nm
                    fix("~2a0f1", 33.30, -111.70, 2000),        // TIS-B
                    fix("abcdef", 33.3055, -111.6569, 52000),   // AZA, altitude clamps at 51,000
                    fix("012345", 33.2691, -111.8111, 2200, 20)};   // stale
  uint8_t buf[4 + 4 * TRAILS_POINT_BYTES];
  const size_t len = trailsEncodePoll(ac, 4, 45123, bLat, bLon, buf, sizeof(buf));
  CHECK(len == 4 + 2 * TRAILS_POINT_BYTES, "2 of 4 kept (TIS-B and stale dropped)");
  size_t off = 0;
  TrailsRec r;
  CHECK(trailsNextRec(buf, len, off, r) && r.n == 2 && r.secOfDay == 45122 && off == len, "record header decodes (2 s units)");
  const TrailsPoint p0 = trailsPoint(r, 0, bLat, bLon), p1 = trailsPoint(r, 1, bLat, bLon);
  CHECK(p0.icao == 0xa1b2c3 && std::fabs(p0.lat - 33.4342) <= 0.00026 && std::fabs(p0.lon + 112.0116) <= 0.00026,
        "position round-trips within half a unit (~25 m)");
  CHECK(p0.altFt == 3400 && p1.altFt == 255 * TRAILS_ALT_UNIT_FT, "altitude in 200 ft steps, clamped");
  CHECK(!trailsNextRec(buf, len, off, r), "end of buffer");
  buf[0] = 0x00;
  off = 0;
  CHECK(!trailsNextRec(buf, len, off, r), "a corrupt record is refused");

  uint8_t hdr[TRAILS_HDR_BYTES];
  trailsHeader(20260926, bLat, bLon, hdr);
  CHECK(!memcmp(hdr, "SKT1", 4) && hdr[4] == (20260926 & 0xFF), "day header");

  Aircraft many[80];
  for (int i = 0; i < 80; i++) {
    char h[7];
    std::snprintf(h, sizeof(h), "%06x", 0x100000 + i);
    many[i] = fix(h, bLat + i * 0.001, bLon, 10000);
  }
  uint8_t big[4 + 80 * TRAILS_POINT_BYTES];
  CHECK(trailsEncodePoll(many, 80, 0, bLat, bLon, big, sizeof(big)) == sizeof(big), "80 aircraft fit one record buffer");
  CHECK(trailsEncodePoll(ac + 1, 1, 0, bLat, bLon, buf, sizeof(buf)) == 0, "nothing honest -> nothing written");
  Aircraft farOff = fix("a1b2c4", 60.0, -111.79, 30000);   // ~27 deg away: out of the i16 range
  CHECK(trailsEncodePoll(&farOff, 1, 0, bLat, bLon, buf, sizeof(buf)) == 0, "out of range: skipped, never clamped");
}
