// Host tests for Today's Sky: spotter.cpp + chip.cpp (docs/11-today.md -> Tests).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "chip.h"
#include "geo.h"
#include "route_client.h"
#include "spotter.h"
#include "tracker.h"

extern int g_fail;
#define CHECK(cond, msg)                                                             \
  do {                                                                               \
    if (!(cond)) { std::printf("  FAIL: %s (line %d)\n", msg, __LINE__); g_fail++; } \
    else std::printf("  ok:   %s\n", msg);                                          \
  } while (0)

namespace {

Aircraft make(const char *hex, double lat, double lon, int alt, float track, float gs, const char *type = "B738",
              const char *cs = "SWA1637") {
  Aircraft a{};
  strncpy(a.hex, hex, sizeof(a.hex) - 1);
  strncpy(a.callsign, cs, sizeof(a.callsign) - 1);
  strncpy(a.type, type, sizeof(a.type) - 1);
  strcpy(a.reg, "N8563Z");
  a.lat = lat; a.lon = lon; a.altFt = alt; a.track = track; a.gsKt = gs;
  double d, az;
  geo::distBearing({OBS_LAT, OBS_LON}, {lat, lon}, d, az);
  a.distNm = d / geo::M_PER_NM;
  a.azDeg = az;
  a.elDeg = geo::elevation(d, alt, OBS_ELEV_FT);
  return a;
}

// Northbound along a meridian `eastNm` east of the observer; heading may flip south.
Aircraft flyover(const char *hex, double tSec, double startNm, double eastNm, int alt, float gs, bool south = false,
                 const char *type = "B738") {
  const geo::LatLon east = geo::destination({OBS_LAT, OBS_LON}, 90, eastNm * geo::M_PER_NM);
  const double northM = (south ? 1 : -1) * startNm * geo::M_PER_NM + (south ? -1 : 1) * gs * 0.514444 * tSec;
  const geo::LatLon p = geo::destination(east, northM >= 0 ? 0 : 180, fabs(northM));
  return make(hex, p.lat, p.lon, alt, south ? 180 : 0, gs, type);
}

Traffic g_t;
uint32_t g_epoch0 = 1790000000;   // any synced time
SpotterClock clk(uint32_t ms, uint32_t day = 20260925, uint8_t hour = 19) {
  return {true, g_epoch0 + ms / 1000, day, hour};
}

void step(uint32_t ms, std::initializer_list<Aircraft> acs, const SpotterClock &c, uint8_t fail = 0,
          uint8_t provider = 0) {
  g_t.n = 0;
  for (const auto &a : acs) g_t.ac[g_t.n++] = a;
  g_t.fetchedMs = ms;
  g_t.ok = fail == 0;
  g_t.failStreak = fail;
  g_t.provider = provider;
  g_t.version++;
  trackerUpdate(g_t, ms);
  spotterUpdate(g_t, true, fail < 3, c, ms);
}

void labelFn(const Aircraft &a, PassLabel &out) { spotterLabels(a, nullptr, out); }

void quiet(uint32_t &ms, uint32_t day = 20260925, uint8_t hour = 19) {   // let the tracker drain
  for (int i = 0; i < 12; i++, ms += 5000) step(ms, {}, clk(ms, day, hour));
}

// One northbound flyover from 8 nm south, 5 s polls; returns the ms after it.
uint32_t fly(const char *hex, uint32_t ms, double eastNm = 0.2, float gs = 220, bool south = false,
             uint32_t day = 20260925, uint8_t hour = 19, const char *type = "B738") {
  for (double s = 0; s <= 280; s += 5, ms += 5000)
    step(ms, {flyover(hex, s, 8, eastNm, 5000, gs, south, type)}, clk(ms, day, hour));
  return ms;
}

int16_t w6(const char *s) { return (int16_t)(7 * strlen(s)); }   // ~f2 pitch

}  // namespace

void testToday() {
  std::printf("today: one flyover = one pass, a kept promise\n");
  spotterInit(labelFn);
  uint32_t ms = 1000;
  ms = fly("a1b2c3", ms);
  quiet(ms);
  const TodaySummary &s = spotterSummary();
  CHECK(s.overhead == 1 && s.hourly[19] == 1, "one pass counted in hour 19");
  CHECK(s.nRecent == 1 && strcmp(s.recent[0].hex, "a1b2c3") == 0, "pass in the recent list");
  CHECK(s.recent[0].peakEl > 70, "peak elevation followed");
  CHECK(s.recent[0].toward == 0, "toward N");
  CHECK(s.recent[0].closeEpoch > s.recent[0].peakEpoch, "close after peak");
  CHECK(s.today.kept == 1 && s.today.broken == 0, "straight flyover: its promise was kept");
  PassRec pr;
  CHECK(!spotterTakePassLine(pr, clk(ms).epoch), "no CSV line inside the merge window");
  CHECK(spotterTakePassLine(pr, clk(ms).epoch + TODAY_MERGE_S) && pr.logged, "CSV line after the window");
  CHECK(!spotterTakePassLine(pr, clk(ms).epoch + TODAY_MERGE_S), "logged once");
  PromiseLine pl;
  CHECK(spotterTakePromiseLine(pl) && pl.outcome == PromiseOutcome::Kept && pl.leadS > 0, "promise line");

  std::printf("today: the same plane back within 10 min is the same pass\n");
  spotterInit(labelFn);
  ms = 1000;
  ms = fly("d4e5f6", ms);
  quiet(ms);
  ms = fly("d4e5f6", ms, 0.2, 220, true);                       // back southbound
  quiet(ms);
  CHECK(spotterSummary().overhead == 1 && spotterSummary().nRecent == 1, "re-entry merged");
  ms += TODAY_MERGE_S * 1000UL + 1000;
  ms = fly("d4e5f6", ms);
  quiet(ms);
  CHECK(spotterSummary().overhead == 2, "after the window: a new pass");

  std::printf("today: 4+ passes inside 10 min - the first plane back still merges (review B1)\n");
  spotterInit(labelFn);
  ms = 1000;
  ms = fly("ee0001", ms);
  for (double s = 0; s <= 150; s += 5, ms += 5000)             // three at once: pushes ee0001
    step(ms, {flyover("ee0002", s, 3, 0.3, 5000, 220), flyover("ee0003", s, 3, 0.5, 5000, 220),   // out of `recent`
              flyover("ee0004", s, 3, -0.3, 5000, 220)}, clk(ms));
  const uint16_t before4 = spotterSummary().overhead;
  CHECK(before4 == 4 && spotterSummary().nRecent == 3, "four passes, three in the recent list");
  CHECK(!spotterTakePassLine(pr, clk(ms).epoch), "a parked pass is not logged inside its window");
  for (double s = 0; s <= 150; s += 5, ms += 5000) step(ms, {flyover("ee0001", s, 3, 0.2, 5000, 220, true)}, clk(ms));
  quiet(ms);
  CHECK(spotterSummary().overhead == before4, "the parked pass re-opened: not counted twice");
  int lines = 0;
  while (spotterTakePassLine(pr, clk(ms).epoch + TODAY_MERGE_S)) lines++;
  CHECK(lines == 4, "one CSV line per plane");

  std::printf("today: a tapped (forced) plane is not a pass\n");
  spotterInit(labelFn);
  ms = 1000;
  const Aircraft far = make("0f0f0f", OBS_LAT + 0.08, OBS_LON, 9000, 90, 250);
  step(ms, {far}, clk(ms));
  trackerForceHex(g_t, "0f0f0f", ms);
  for (int i = 0; i < 6; i++, ms += 5000) step(ms, {far}, clk(ms));
  quiet(ms);
  CHECK(spotterSummary().overhead == 0, "forced: not counted");

  std::printf("today: midnight resets counts, not the last passes\n");
  spotterInit(labelFn);
  spotterSetLogging(true);
  ms = 1000;
  ms = fly("aa0001", ms, 0.2, 220, false, 20260925, 23);
  quiet(ms, 20260925, 23);
  step(ms, {}, clk(ms, 20260926, 0));
  CHECK(spotterSummary().overhead == 0 && spotterSummary().hourly[23] == 0, "counts reset");
  CHECK(spotterSummary().nRecent == 1, "last passes survive midnight");
  DayLine dl;
  CHECK(spotterTakeDayLine(dl) && dl.dayKey == 20260925 && dl.overhead == 1, "day line for yesterday");
  CHECK(spotterSummary().ring[0].kept == 1 && spotterSummary().historyDays == 1, "promise ring rolled");
  char line[160];
  spotterCsvDay(dl, line, sizeof(line));
  CHECK(strncmp(line, "2026-09-25,1,", 13) == 0, "days.csv line");
  ms += 5000;
  step(ms, {}, clk(ms, 20260930, 8));                   // off for 4 days
  CHECK(spotterSummary().ring[0].kept == 0 && spotterSummary().ring[4].kept == 1,
        "days off are empty days in the 7-day gate ring");
  CHECK(spotterDaysBetween(20260228, 20260301) == 1 && spotterDaysBetween(20241231, 20250101) == 1 &&
        spotterDaysBetween(20240228, 20240301) == 2, "calendar day math (leap years)");
  spotterSetLogging(false);
  ms += 5000;
  step(ms, {}, clk(ms, 20261001, 8));
  const uint16_t hd = spotterSummary().historyDays;
  ms += 5000;
  step(ms, {}, clk(ms, 20261002, 8));
  CHECK(spotterSummary().historyDays == hd, "a day without the log doesn't end 'learning'");

  std::printf("today: nearby estimate, filters\n");
  spotterInit(labelFn);
  ms = 1000;
  srand(7);
  int fed = 0;
  while (fed < 1500) {
    g_t.n = 0;
    for (int k = 0; k < 40 && fed < 1500; k++, fed++) {
      char hex[7];
      snprintf(hex, sizeof(hex), "%06x", (unsigned)(rand() & 0xFFFFFF) ^ (unsigned)fed);
      Aircraft a = make(hex, OBS_LAT + 0.05, OBS_LON, 20000, 0, 400, "A320");
      g_t.ac[g_t.n++] = a;
    }
    g_t.fetchedMs = ms; g_t.ok = true; g_t.failStreak = 0; g_t.version++;
    spotterUpdate(g_t, true, true, clk(ms), ms);
    ms += 5000;
  }
  const double est = spotterNearby();
  std::printf("        1500 fed -> estimate %.0f (%.1f %%)\n", est, 100 * fabs(est - 1500) / 1500);
  CHECK(fabs(est - 1500) / 1500 < 0.03, "linear counting within 3 % at 1,500");
  const uint16_t before = spotterNearby();
  Aircraft ground = make("ab0001", OBS_LAT + 0.01, OBS_LON, 1300, 0, 5, "A320");
  ground.onGround = true;
  Aircraft farAway = make("ab0002", OBS_LAT + 0.5, OBS_LON, 20000, 0, 400, "A320");   // ~30 nm
  step(ms, {ground, farAway}, clk(ms));
  CHECK(spotterNearby() == before, "ground and > 12 nm ignored");
  char q[5];
  while (spotterTakeTypeQuery(q)) {}
  Aircraft twr = make("ab0003", OBS_LAT + 0.01, OBS_LON, 3000, 0, 0, "TWR");
  Aircraft veh = make("ab0004", OBS_LAT + 0.01, OBS_LON, 3000, 0, 0, "A320");
  strcpy(veh.category, "C1");
  ms += 5000;
  step(ms, {twr, veh}, clk(ms));
  CHECK(!spotterTakeTypeQuery(q), "junk types never become rarest candidates");

  std::printf("today: promises - missed, excluded, gate\n");
  spotterInit(labelFn);
  ms = 1000;
  // inbound, then turns away east before the pop point
  for (double s = 0; s <= 30; s += 5, ms += 5000) step(ms, {flyover("bb0001", s, 5, 0.1, 5000, 220)}, clk(ms));
  for (int i = 0; i < 20; i++, ms += 5000) {
    Aircraft a = make("bb0001", OBS_LAT - 0.03, OBS_LON + 0.01 * (i + 1), 5000, 90, 220);
    step(ms, {a}, clk(ms));
  }
  quiet(ms);
  CHECK(spotterSummary().today.broken == 1 && spotterSummary().overhead == 0, "a turn-away is a broken promise");
  spotterInit(labelFn);
  ms = 1000;
  for (double s = 0; s <= 280; s += 5, ms += 5000)
    step(ms, {flyover("bb0002", s, 8, 0.2, 5000, 220)}, clk(ms), s == 100 ? 1 : 0);
  quiet(ms);
  CHECK(spotterSummary().today.excluded == 1 && spotterSummary().today.kept == 0, "a feed failure excludes it");
  TodaySummary g{};
  g.magic = TODAY_MAGIC;
  g.ring[0] = {20, 5, 3};
  g.ring[1] = {6, 1, 0};
  spotterRestore(g, nullptr, 0);
  uint16_t n = 0, ex = 0;
  CHECK(spotterGatePct(TODAY_GATE_MIN_N, &n, &ex) == 81 && n == 32 && ex == 3, "gate 26/32 = 81 %, excluded reported");
  g.ring[1] = {};
  spotterRestore(g, nullptr, 0);
  CHECK(spotterGatePct(TODAY_GATE_MIN_N) == -1, "gate: too few promises");

  std::printf("today: promises - the first promise wins (review B2)\n");
  spotterInit(labelFn);
  ms = 1000;
  // inbound, then it nearly stops (the promise times out), then it comes overhead after all
  for (double s = 0; s <= 25; s += 5, ms += 5000) step(ms, {flyover("bb0003", s, 5, 0.1, 5000, 220)}, clk(ms));
  const Aircraft slow = flyover("bb0003", 25, 5, 0.1, 5000, 220);
  for (int i = 0; i < 16; i++, ms += 5000) {
    Aircraft a = slow;
    a.gsKt = 20;
    step(ms, {a}, clk(ms));
  }
  for (double s = 25; s <= 250; s += 5, ms += 5000) step(ms, {flyover("bb0003", s, 5, 0.1, 5000, 220)}, clk(ms));
  quiet(ms);
  CHECK(spotterSummary().today.broken == 1 && spotterSummary().today.kept == 0,
        "late plane: one broken promise, no second 'kept' one");

  std::printf("today: rarest\n");
  spotterInit(labelFn);
  step(1000, {}, clk(1000));
  spotterTypeAnswer("B38M", 900, 40, 20260925);
  CHECK(spotterSummary().rarest[0] == 0, "common type (seen many days) never rarest: none new");
  spotterTypeAnswer("A35K", 2, 1, 20260925);
  spotterTypeAnswer("C17", 5, 2, 20260925);
  CHECK(strcmp(spotterSummary().rarest, "A35K") == 0, "fewest sightings wins");
  spotterTypeAnswer("H60", 2, 0, 20260925);
  CHECK(strcmp(spotterSummary().rarest, "H60") == 0, "tie: the newest wins");
  spotterTypeAnswer("C130", 0, 0, 20260924);
  CHECK(strcmp(spotterSummary().rarest, "H60") == 0, "an answer for another day is ignored");

  std::printf("today: privacy and CSV\n");
  Aircraft ga = make("cc0001", OBS_LAT, OBS_LON, 4000, 0, 100, "C172", "N172SP");
  RouteInfo r{};
  r.found = true;
  strcpy(r.owner, "JOHN Q SMITH");
  PassRec p{};
  strcpy(p.hex, "cc0001");
  spotterLabels(ga, &r, p.lab);
  spotterCsvPass(p, nullptr, line, sizeof(line));
  CHECK(!strstr(line, "SMITH") && !strstr(line, "Smith"), "GA owner never in the CSV");
  Aircraft p91 = make("cc0003", OBS_LAT, OBS_LON, 30000, 0, 400, "GLF6", "XYZ123");   // airline-like callsign
  RouteInfo pr91{};
  pr91.found = true;
  strcpy(pr91.owner, "SMITH FAMILY TRUSTEE");
  spotterLabels(p91, &pr91, p.lab);
  spotterCsvPass(p, nullptr, line, sizeof(line));
  CHECK(p.lab.op[0] == 0 && !strstr(line, "Smith") && !strstr(line, "SMITH"),
        "Part 91 jet on an airline-like callsign: owner never becomes the operator (review)");
  Aircraft al = make("cc0002", OBS_LAT, OBS_LON, 9000, 0, 300, "A35K", "BAW3GB");
  RouteInfo br{};
  br.found = br.hasRoute = true;
  strcpy(br.airline, "British Airways, PLC");
  strcpy(br.flightIata, "BA289");
  strcpy(br.origIata, "LHR"); strcpy(br.destIata, "SYD");
  br.oLat = 51.47; br.oLon = -0.45; br.dLat = -33.94; br.dLon = 151.17;   // nowhere near Phoenix
  spotterLabels(al, &br, p.lab);
  CHECK(p.lab.orig[0] == 0 && strcmp(p.lab.code, "BA") == 0, "implausible route dropped; airline code kept");
  spotterCsvPass(p, nullptr, line, sizeof(line));
  int commas = 0;
  for (const char *c = line; *c; ++c) commas += *c == ',';
  CHECK(commas == 10, "commas in names become spaces (11 fields)");

  std::printf("today: chip arbiter\n");
  ChipInputs ci{};
  ChipMsg m;
  ci.trafficUp = true;
  ci.nNearby = 4; ci.nearestType = "737-800"; ci.nearestMi = 5.2f; ci.nearestDir = "W";
  ci.hasPass = true; ci.passAgeS = 125; ci.passOp = "Southwest"; ci.passType = "737 MAX 8"; ci.passIcao = "B38M";
  ci.passHex = "a1b2c3";
  chipMessage(ci, w6, m);
  CHECK(m.kind == ChipKind::Passed && !strcmp(m.left, "Passed 2 min ago") && !strcmp(m.right, "Southwest 737 MAX 8"),
        "passed beats nearby");
  CHECK(m.chevron && !strcmp(m.focusHex, "a1b2c3"), "passed: tap -> map focused on it");
  ci.passAgeS = 20;
  chipMessage(ci, w6, m);
  CHECK(!strcmp(m.left, "Passed just now"), "just now");
  ci.passOp = "British Airways"; ci.passType = "A350-1000"; ci.passAgeS = 599;
  chipMessage(ci, w6, m);
  CHECK(!strcmp(m.right, "A350-1000"), "worst fit: type alone");
  ci.passAgeS = TODAY_PASSED_S;
  chipMessage(ci, w6, m);
  CHECK(m.kind == ChipKind::Nearby && m.focusHex[0] == 0, "expires after 10 min from close");
  ci.trafficUp = false; ci.retryS = 30; ci.passAgeS = 60;
  chipMessage(ci, w6, m);
  CHECK(m.kind == ChipKind::Offline && !m.chevron, "offline beats passed");

  ci.trafficUp = true; ci.passAgeS = 599; ci.passCode = "BA";
  chipMessage(ci, w6, m);
  CHECK(!strcmp(m.right, "BA A350-1000"), "worst fit: code + type, like the Today row");

  std::printf("today: header entry fit (round 2 M1)\n");
  char he[16];
  CHECK(headerEntryFit(31, 90, SCREEN_W, w6, he) && !strcmp(he, "31 overhead"), "no status: '31 overhead'");
  const int16_t statusLeft = 296 - w6("Updated 47 min ago");
  CHECK(headerEntryFit(104, 90, statusLeft, w6, he) && !strcmp(he, "104"), "status showing: '104'");
  CHECK(headerEntryFit(1204, 90, statusLeft, w6, he) && !strcmp(he, "1.2k"), "status showing: '1.2k'");
  CHECK(headerEntryFit(0, 90, statusLeft, w6, he) && !strcmp(he, "0"), "status showing: '0'");
  CHECK(!headerEntryFit(104, 90, 120, w6, he) && he[0] == 0, "no room: text hidden (target stays)");
  CHECK(headerEntryFit(12345, 90, statusLeft, w6, he) && !strcmp(he, "12k"), "10k and up: no '12.3k'");

  std::printf("today: outage hours, restore without the set\n");
  spotterInit(labelFn);
  ms = 1000;
  for (int i = 0; i < 80; i++, ms += 5000) step(ms, {}, clk(ms, 20260925, 14), 3);
  CHECK(spotterSummary().outageHours == (1UL << 14), "> 5 min offline: hour 14 flagged");
  TodaySummary saved = spotterSummary();
  saved.nearbySaved = 212;
  spotterRestore(saved, nullptr, 0);
  CHECK(spotterNearby() == 212 && spotterSummary().nearbyLost, "no set: 212 carried, marked '~'");
}
