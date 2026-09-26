// Host tests for geo.cpp + tracker.cpp (pure logic). Run: test/host/run.sh
// Simulates aircraft positions exactly the way adsb_client.cpp enriches them.
#include <cassert>

#include "geo.h"
#include "tracker.h"

HostSerial Serial;

int g_fail = 0;
void testParse();
void testPlausible();
void testLabels();
void testMap();
void testRadar();
void testToday();
void testSetup();
void testTrails();
void testTouchMap();
#define CHECK(cond, msg)                                              \
  do {                                                                \
    if (!(cond)) { std::printf("  FAIL: %s (line %d)\n", msg, __LINE__); g_fail++; } \
    else std::printf("  ok:   %s\n", msg);                            \
  } while (0)

static Aircraft make(const char *hex, double lat, double lon, int alt, float track, float gs) {
  Aircraft a{};
  strncpy(a.hex, hex, sizeof(a.hex) - 1);
  strcpy(a.callsign, "TST123");
  a.lat = lat; a.lon = lon; a.altFt = alt; a.track = track; a.gsKt = gs;
  double d, az;
  geo::distBearing({OBS_LAT, OBS_LON}, {lat, lon}, d, az);
  a.distNm = d / geo::M_PER_NM;
  a.azDeg = az;
  a.elDeg = geo::elevation(d, alt, OBS_ELEV_FT);
  return a;
}

// Aircraft flying north along the observer's meridian, offset east by `eastNm`.
static Aircraft flyover(const char *hex, double tSec, double startNm, double eastNm, int alt, float gs) {
  const geo::LatLon east = geo::destination({OBS_LAT, OBS_LON}, 90, eastNm * geo::M_PER_NM);
  const double northM = -startNm * geo::M_PER_NM + gs * 0.514444 * tSec;
  const geo::LatLon p = geo::destination(east, northM >= 0 ? 0 : 180, fabs(northM));
  return make(hex, p.lat, p.lon, alt, 0, gs);
}

static void feed(Traffic &t, uint32_t now, std::initializer_list<Aircraft> acs) {
  t.n = 0;
  for (const auto &a : acs) t.ac[t.n++] = a;
  t.fetchedMs = now;
  t.version++;
}

static void testGeo() {
  std::printf("geo test vectors (docs/05 §8)\n");
  Serial.quiet = true;
  CHECK(geo::selfTest(), "selfTest vectors");
  Serial.quiet = false;
  CHECK(strcmp(geo::compass8(44), "NE") == 0 && strcmp(geo::compass8(359), "N") == 0, "compass8");
  CHECK(strcmp(geo::relativeWords(20, 200), "behind you") == 0, "relativeWords behind");
}

static void testFlyover() {
  std::printf("single flyover: weather -> plane -> departing -> weather\n");
  Traffic t{};
  uint32_t now = 1000;
  bool sawLive = false, sawDeparting = false;
  int arrivals = 0;
  uint32_t liveAt = 0, departAt = 0, weatherAt = 0;
  // 250 kt northbound at 12,000 ft, passing 0.5 nm east of the observer.
  for (double s = 0; s < 240; s += 2) {
    now = 1000 + (uint32_t)(s * 1000);
    feed(t, now, {flyover("aaa111", s, 8.0, 0.5, 12000, 250)});
    trackerUpdate(t, now);
    if (trackerTakeArrival()) arrivals++;
    const PlaneMode m = trackerView().mode;
    if (m == PlaneMode::Live && !sawLive) { sawLive = true; liveAt = now; }
    if (m == PlaneMode::Departing && !sawDeparting) { sawDeparting = true; departAt = now; }
    if (sawDeparting && m == PlaneMode::None && !weatherAt) weatherAt = now;
  }
  CHECK(sawLive, "plane screen shown");
  CHECK(arrivals == 1, "exactly one arrival cue");
  CHECK(sawDeparting, "departing grace entered");
  CHECK(weatherAt && weatherAt - departAt >= DEPART_GRACE_S * 1000, "grace lasted >= DEPART_GRACE_S");
  CHECK(weatherAt - liveAt >= MIN_PLANE_DWELL_S * 1000, "dwell >= MIN_PLANE_DWELL_S");
  CHECK(!trackerView().stale, "not stale at the end");
}

static void testNoPingPong() {
  std::printf("two planes: featured plane must not ping-pong\n");
  Traffic t{};
  int switches = 0;
  char last[8] = "";
  for (double s = 0; s < 120; s += 2) {
    const uint32_t now = 500000 + (uint32_t)(s * 1000);
    // Two similar jets side by side: elevations within a few degrees of each other.
    feed(t, now, {flyover("bbb222", s, 6.0, 0.6, 14000, 240), flyover("ccc333", s, 6.1, -0.7, 14500, 240)});
    trackerUpdate(t, now);
    const TrackView &v = trackerView();
    if (v.mode == PlaneMode::Live) {
      if (last[0] && strcmp(last, v.ac.hex) != 0) switches++;
      strcpy(last, v.ac.hex);
    }
  }
  CHECK(switches == 0, "no feature switches between near-equal planes");
}

static void testLostAndStale() {
  std::printf("feed drops the plane: stale, then lost -> departing -> weather\n");
  Traffic t{};
  uint32_t now = 900000;
  // Park a plane overhead for a while.
  for (int i = 0; i < 10; i++, now += 2000) {
    feed(t, now, {make("ddd444", OBS_LAT + 0.01, OBS_LON, 8000, 90, 200)});
    trackerUpdate(t, now);
  }
  CHECK(trackerView().mode == PlaneMode::Live, "live while in feed");
  bool sawStale = false, sawNone = false;
  const uint32_t dropAt = now;
  for (; now < dropAt + 60000; now += 1000) {
    feed(t, now, {});                        // plane vanished from the feed
    trackerUpdate(t, now);
    if (trackerView().stale) sawStale = true;
    if (trackerView().mode == PlaneMode::None) { sawNone = true; break; }
  }
  CHECK(sawStale, "stale shown before it is dropped");
  CHECK(sawNone && now - dropAt >= LOST_TIMEOUT_S * 1000, "back to weather only after LOST_TIMEOUT_S");
}

static void testFrozenFeed() {
  std::printf("network dies: frozen snapshot must not flap between plane and weather\n");
  Traffic t{};
  uint32_t now = 3000000;
  feed(t, now, {make("abc999", OBS_LAT + 0.01, OBS_LON, 8000, 90, 200)});
  trackerUpdate(t, now);
  CHECK(trackerView().mode == PlaneMode::Live, "live");
  int changes = 0;
  PlaneMode last = trackerView().mode;
  for (int i = 0; i < 4000; i++) {          // 100 s of 25 ms UI ticks, same old snapshot
    now += 25;
    trackerUpdate(t, now);
    if (trackerView().mode != last) { changes++; last = trackerView().mode; }
  }
  CHECK(last == PlaneMode::None, "ends on weather");
  CHECK(changes == 2, "exactly Live -> Departing -> None (no flapping)");
}

static void testDismissAndForce() {
  std::printf("dismiss + force-show\n");
  Traffic t{};
  uint32_t now = 2000000;
  const Aircraft a = make("eee555", OBS_LAT + 0.01, OBS_LON, 8000, 90, 200);
  feed(t, now, {a});
  trackerUpdate(t, now);
  CHECK(trackerView().mode == PlaneMode::Live, "live");
  trackerDismiss(now);
  now += 2000;
  feed(t, now, {a});
  trackerUpdate(t, now);
  CHECK(trackerView().mode == PlaneMode::None, "dismissed plane does not re-trigger");

  const Aircraft far = make("fff666", OBS_LAT + 0.08, OBS_LON, 5000, 180, 200);   // ~4.8 nm, low
  now += 2000;
  feed(t, now, {far});
  trackerUpdate(t, now);
  CHECK(trackerView().mode == PlaneMode::None, "far plane does not qualify");
  CHECK(trackerForceNearest(t, now), "force-show nearest");
  trackerUpdate(t, now);
  CHECK(trackerView().mode == PlaneMode::Forced, "forced mode");
  now += (FORCED_SHOW_S + 1) * 1000;
  feed(t, now, {far});
  trackerUpdate(t, now);
  CHECK(trackerView().mode == PlaneMode::None, "forced times out to weather");
}

int main() {
  testGeo();
  testFlyover();
  testNoPingPong();
  testLostAndStale();
  testFrozenFeed();
  testDismissAndForce();
  testParse();
  testPlausible();
  testLabels();
  testMap();
  testRadar();
  testToday();
  testSetup();
  testTrails();
  testTouchMap();
  std::printf(g_fail ? "\n%d FAILED\n" : "\nall passed\n", g_fail);
  return g_fail ? 1 : 0;
}
