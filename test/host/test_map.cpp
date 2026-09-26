// Host tests for map_model.cpp (projection, trails, hit-testing).
#include "geo.h"
#include "map_model.h"
#include <string.h>

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
  j.track = 0;

  // ---- v3 (docs/09) --------------------------------------------------------
  {   // M1a: 5 s sampling returns the FIRST qualifying time, and that point qualifies
    WillPop w;
    CHECK(mapWillPop(j, w) && w.secs % MAP_POP_STEP_S == 0, "will-pop time is on the 5 s grid");
    double d, b;
    geo::distBearing({OBS_LAT, OBS_LON}, {w.lat, w.lon}, d, b);
    CHECK(mapQualifies(d / geo::M_PER_NM, geo::elevation(d, j.altFt, OBS_ELEV_FT), j.altFt),
          "the pop point itself passes the ENTER test");
    if (w.secs > MAP_POP_STEP_S) {
      double la, lo;
      mapAhead(j, w.secs - MAP_POP_STEP_S, la, lo);
      geo::distBearing({OBS_LAT, OBS_LON}, {la, lo}, d, b);
      CHECK(!mapQualifies(d / geo::M_PER_NM, geo::elevation(d, j.altFt, OBS_ELEV_FT), j.altFt),
            "the sample 5 s earlier does not qualify (first hit)");
    }
  }
  auto fill = [](Aircraft &a) {
    double d, b;
    geo::distBearing({OBS_LAT, OBS_LON}, {a.lat, a.lon}, d, b);
    a.distNm = d / geo::M_PER_NM; a.azDeg = b; a.elDeg = geo::elevation(d, a.altFt, OBS_ELEV_FT);
  };
  {   // M1: focus order and hold
    Traffic ft{};
    const geo::LatLon np = geo::destination({OBS_LAT, OBS_LON}, 270, 1.0 * geo::M_PER_NM);
    ft.ac[0] = at("near01", np.lat, np.lon);                   // nearest, slow, not inbound
    ft.ac[0].altFt = OBS_ELEV_FT + 1500; ft.ac[0].track = 270; ft.ac[0].gsKt = 90; fill(ft.ac[0]);
    ft.ac[1] = j; strcpy(ft.ac[1].hex, "jet250"); fill(ft.ac[1]);
    ft.n = 2;
    FocusHold hold{};
    CHECK(mapPickFocus(ft, "", "", hold, true) == 1, "focus: the will-pop jet beats the nearest plane");
    CHECK(mapPickFocus(ft, "near01", "", hold, false) == 0, "focus: a selection beats will-pop");
    // a second, sooner will-pop plane appears: the held focus is NOT swapped
    ft.ac[2] = j; strcpy(ft.ac[2].hex, "jet999");
    const geo::LatLon s2 = geo::destination({j.lat, j.lon}, 0, 1.0 * geo::M_PER_NM);
    ft.ac[2].lat = s2.lat; ft.ac[2].lon = s2.lon; fill(ft.ac[2]);
    ft.n = 3;
    CHECK(mapWillPopSecs(ft.ac[2]) && mapWillPopSecs(ft.ac[2]) < mapWillPopSecs(ft.ac[1]), "second jet is sooner");
    CHECK(mapPickFocus(ft, "", "", hold, true) == 1, "focus: a held will-pop plane is not swapped for a sooner one");
    ft.ac[1].track = 180;                                        // the held jet turns away
    CHECK(mapPickFocus(ft, "", "", hold, true) == 1, "focus: held through 1 non-will-pop poll");
    CHECK(mapPickFocus(ft, "", "", hold, true) == 2, "focus: dropped after 2, the sooner jet takes over");
    // M7 precedence: will-pop beats the just-popped plane; with no will-pop, it wins over nearest
    CHECK(mapPickFocus(ft, "", "near01", hold, true) == 2, "focus: will-pop beats the just-popped plane");
    ft.ac[2].track = 180;
    FocusHold h2{};
    CHECK(mapPickFocus(ft, "", "near01", h2, true) == 0, "focus: just-popped plane when nothing will pop");
    CHECK(mapPickFocus(ft, "", "", h2, true) == -1, "focus: nothing -> caller uses the nearest on the map");
    // 3.0 chip-passed tier (docs/09 M1, docs/11): below just-popped, above nearest
    CHECK(mapPickFocus(ft, "", "", h2, true, "jet250") == 1, "focus: chip-passed plane beats nearest");
    CHECK(mapPickFocus(ft, "", "near01", h2, true, "jet250") == 0, "focus: just-popped beats chip-passed");
    CHECK(mapPickFocus(ft, "jet999", "", h2, true, "jet250") == 2, "focus: a selection beats chip-passed");
    CHECK(mapPickFocus(ft, "", "", h2, true, "gone01") == -1, "focus: chip-passed plane left the traffic -> normal");
    ft.ac[2].track = 0;                                          // a will-pop plane again
    FocusHold h3{};
    CHECK(mapPickFocus(ft, "", "", h3, true, "jet250") == 2, "focus: will-pop beats chip-passed");
  }
  {   // M1b: idle pause capped, two stamps (v3-R3-2)
    uint32_t touch = 1000;
    CHECK(!mapIdleExpired(touch + 119000, touch, touch, false), "idle: not expired at 119 s");
    CHECK(mapIdleExpired(touch + 121000, touch, touch, false), "idle: expired at 121 s");
    bool expiredEarly = false;                           // a stream of will-pop planes keeps it paused ...
    for (uint32_t s = 121; s <= 300; s += 5) expiredEarly |= mapIdleExpired(touch + s * 1000, touch, touch, true);
    CHECK(!expiredEarly, "idle: paused by a will-pop focus up to 300 s");
    CHECK(mapIdleExpired(touch + 301000, touch, touch, true), "idle: ... but never beyond MAP_IDLE_MAX_S");
    // a card pops every 90 s and each return restarts the 120 s timer - the cap still holds
    bool heldPast = false, expiredBy = false;
    uint32_t timer = touch;
    for (uint32_t s = 0; s <= 400; s++) {
      if (s && s % 90 == 0) timer = touch + s * 1000;     // back from a card
      const bool e = mapIdleExpired(touch + s * 1000, timer, touch, true);
      if (s <= 300) heldPast |= e;
      if (s == 301) expiredBy = e;
    }
    CHECK(!heldPast && expiredBy, "idle: pops every 90 s cannot hold the map past 300 s after the last touch");
    CHECK(!mapIdleExpired(5000, 0xFFFFF000u, 0xFFFFF000u, false), "idle: wrap-safe across millis() rollover");
  }
  {   // M3: tap candidates ordered by distance from the TAP, not from you
    Traffic ct{};
    for (int i = 0; i < 3; i++) {
      const geo::LatLon p2 = geo::destination({OBS_LAT, OBS_LON}, 90, (3.0 + i * 0.8) * geo::M_PER_NM);
      ct.ac[i] = at(i == 0 ? "cand00" : i == 1 ? "cand01" : "cand02", p2.lat, p2.lon);
    }
    ct.n = 3;
    int8_t out[8];
    // 10 px/nm: planes at x = +30, +38, +46 px. Tap at +45 -> order 2, 1, 0
    const uint8_t n = mapTapCandidates(ct, 10, MAP_CX + 45, MAP_CY, 28, out, 8);
    CHECK(n == 3 && out[0] == 2 && out[1] == 1 && out[2] == 0, "tap candidates sorted by distance from the tap");
    CHECK(mapTapCandidates(ct, 10, MAP_CX + 45, MAP_CY, 5, out, 8) == 1, "tap radius limits candidates");
    // M3: frozen cycle around the first tap's ANCHOR (v3-R2-3)
    TapCycle c{};
    uint8_t kk, nn;
    int sel = mapTap(ct, 10, MAP_CX + 45, MAP_CY, 1000, "", c, kk, nn);
    CHECK(sel == 2 && kk == 1 && nn == 3, "tap: first tap selects the nearest to the tap, 1 of 3");
    CHECK(mapTap(ct, 10, MAP_CX + 45, MAP_CY, 1100, ct.ac[2].hex, c, kk, nn) == -2, "tap: < 250 ms is a bounce");
    // N+1 taps at the same spot: 1, 2, 3, then 1 (the focus is NOT the nearest to the finger)
    const char *focus = ct.ac[sel].hex;
    int seq[4];
    for (int i = 0; i < 4; i++) {
      seq[i] = mapTap(ct, 10, MAP_CX + 44, MAP_CY + 3, 2000 + i * 1000, focus, c, kk, nn);
      focus = ct.ac[seq[i]].hex;
      if (i == 0) ct.ac[1].lat += 0.0005;                                            // a poll moves planes
    }
    CHECK(seq[0] == 1 && seq[1] == 0 && seq[2] == 2 && seq[3] == 1, "tap: same spot cycles 2, 3, 1, 2 (wraps)");
    sel = mapTap(ct, 10, MAP_CX + 5, MAP_CY, 7000, focus, c, kk, nn);                 // 40 px away
    CHECK(sel == 0 && kk == 1, "tap: 40 px from the anchor starts a new list");
    ct.n = 2;                                                                        // cand02 leaves
    sel = mapTap(ct, 10, MAP_CX + 45, MAP_CY, 8000, "cand00", c, kk, nn);
    CHECK(sel >= 0 && nn <= 2, "tap: a candidate that left the traffic is skipped");
    CHECK(mapTap(ct, 10, MAP_CX - 100, MAP_CY, 9000, "", c, kk, nn) == -1, "tap: empty sky clears the selection");
  }
  {   // strip right side (docs/09 table), with the real Font 2 widths
    struct F2 {
      static int16_t w(const char *s) {
        static const uint8_t W[96] = {6,3,4,9,8,9,9,3,7,7,8,6,3,6,5,7,8,8,8,8,8,8,8,8,8,8,3,3,6,6,6,8,9,8,8,8,8,8,8,8,
                                      8,4,8,8,7,10,8,8,8,8,8,8,8,8,8,10,8,8,8,4,7,4,7,9,5,7,7,7,7,7,6,7,7,4,5,6,4,8,7,
                                      8,7,8,6,6,5,7,8,8,6,7,7,5,3,5,8,6};
        int16_t t = 0;
        for (; *s; s++) t += (*s >= 32 && *s < 128) ? W[*s - 32] : 0;
        return t;
      }
    };
    Aircraft a = j;
    a.distNm = 4.7f; a.azDeg = 225; a.elDeg = 20.4f; a.altFt = 7200;
    StripSeg seg[2];
    const int16_t typeW = F2::w("737 MAX 8");
    uint8_t ns = mapStripRight(StripKind::WillPop, a, 0, 0, 45, typeW, F2::w("Southwest"), F2::w, seg);
    CHECK(ns == 1 && strcmp(seg[0].text, "overhead in ~45 s") == 0, "strip: Southwest keeps 'overhead in ~45 s'");
    ns = mapStripRight(StripKind::WillPop, a, 0, 0, 45, typeW, F2::w("Sun Country"), F2::w, seg);
    CHECK(ns == 1 && strcmp(seg[0].text, "in ~45 s") == 0, "strip: Sun Country stays whole -> 'in ~45 s'");
    ns = mapStripRight(StripKind::InDisc, a, 0, 0, 0, typeW, F2::w("Southwest"), F2::w, seg);
    CHECK(ns == 2 && strcmp(seg[1].text, "needs 25`") == 0, "strip: in the disc '20` up . needs 25`'");
    ns = mapStripRight(StripKind::Passed, a, 0, 0, 0, typeW, F2::w("Southwest"), F2::w, seg);
    CHECK(ns == 2 && strcmp(seg[0].text, "passed") == 0, "strip: 'passed . 5.4 mi SW'");
    ns = mapStripRight(StripKind::Cycle, a, 2, 5, 0, typeW, F2::w("Southwest"), F2::w, seg);
    CHECK(ns == 1 && strcmp(seg[0].text, "2 of 5 here") == 0, "strip: '2 of 5 here'");
    ns = mapStripRight(StripKind::Default, a, 0, 0, 0, typeW, F2::w("Southwest"), F2::w, seg);
    CHECK(ns == 2 && strcmp(seg[0].text, "7.2k ft") == 0, "strip: default altitude + distance");
    bool fits = true;                                   // every state fits the type + a 40 px stub
    for (StripKind kd : {StripKind::Cycle, StripKind::WillPop, StripKind::Passed, StripKind::InDisc, StripKind::Default}) {
      ns = mapStripRight(kd, a, 8, 8, 60, F2::w("A350-1000"), F2::w("Sun Country"), F2::w, seg);
      int16_t w = 0;
      for (uint8_t i = 0; i < ns; i++) w += F2::w(seg[i].text) + (i ? 10 : 0);
      fits = fits && 300 - w - 8 - 26 >= F2::w("A350-1000") + 10 + 40;
    }
    CHECK(fits, "strip: every state leaves the type whole plus a 40 px operator stub");
  }
}
