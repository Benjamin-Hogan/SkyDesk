// Host tests: readsb parsing (real captured payloads) + route plausibility.
#include <fstream>
#include <sstream>
#include <string>

#include "adsb_parse.h"
#include "geo.h"
#include "route_client.h"

extern int g_fail;
#define CHECK(cond, msg)                                              \
  do {                                                                \
    if (!(cond)) { std::printf("  FAIL: %s (line %d)\n", msg, __LINE__); g_fail++; } \
    else std::printf("  ok:   %s\n", msg);                            \
  } while (0)

static std::string slurp(const char *name) {
  std::ifstream f(std::string(FIXTURES) + "/" + name);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static Aircraft out[80];

// ByteSource over a string (the device wraps the HTTPS stream instead).
class StringSource : public ByteSource {
 public:
  explicit StringSource(const std::string &s) : s_(s) {}
  int peek() override { return i_ < s_.size() ? (unsigned char)s_[i_] : -1; }
  int read() override { return i_ < s_.size() ? (unsigned char)s_[i_++] : -1; }
  size_t readBytes(char *b, size_t n) override {
    size_t k = 0;
    while (k < n && i_ < s_.size()) b[k++] = s_[i_++];
    return k;
  }

 private:
  const std::string &s_;
  size_t i_ = 0;
};

static int parse(const std::string &json, size_t cap = 80) {
  StringSource src(json);
  return adsbParseStream(src, out, cap);
}

void testParse() {
  std::printf("readsb parsing (captured 2026-09-24 near Gilbert, AZ)\n");
  int n = parse(slurp("adsbfi_gilbert.json"));
  CHECK(n == 9, "adsb.fi: 9 aircraft via root key 'aircraft'");
  bool sorted = true;
  for (int i = 1; i < n; i++) sorted &= out[i - 1].distNm <= out[i].distNm;
  CHECK(sorted, "sorted by distance");
  const Aircraft *swa = nullptr;
  for (int i = 0; i < n; i++)
    if (strcmp(out[i].callsign, "SWA1637") == 0) swa = &out[i];
  CHECK(swa != nullptr, "callsign trimmed ('SWA1637 ' -> 'SWA1637')");
  if (swa) {
    CHECK(swa->altFt == 1700, "alt_geom preferred over alt_baro");
    CHECK(strcmp(swa->type, "B737") == 0 && strcmp(swa->reg, "N429WN") == 0, "type + registration");
    CHECK(swa->vRateFpm == -960 && !swa->onGround, "vertical rate, airborne");
    CHECK(fabs(swa->distNm - 9.9) < 0.3, "distance recomputed from our own observer (~9.9 nm)");
  }

  n = parse(slurp("adsblol_gilbert.json"));
  CHECK(n == 9, "adsb.lol: 9 aircraft via root key 'ac'");

  n = parse(R"({"ac":[
      {"hex":"abc123","flight":"N1 ","alt_baro":"ground","lat":33.3,"lon":-111.7,"seen_pos":1},
      {"hex":"def456","alt_baro":5000},
      {"hex":"a00001","alt_baro":4000,"lat":33.36,"lon":-111.79,"track":90,"gs":120}]})");
  CHECK(n == 1 && strcmp(out[0].hex, "a00001") == 0,
        "ground aircraft and aircraft without a position are dropped");

  n = parse(slurp("adsbfi_gilbert_25nm.json"));
  CHECK(n == 13, "real 25 nm sample: 23 ground aircraft dropped, 13 airborne kept");
  n = parse(slurp("adsbfi_gilbert_35nm.json"));
  const std::string far = out[n - 1].hex;
  CHECK(adsbKeepNearest(out, n, 5, far.c_str()) == 5 && far == out[4].hex,
        "40-cap keeps the pinned (selected) plane even when it is the farthest");
  CHECK(parse(R"({"msg":"nope"})") == -1, "missing aircraft array -> -1");
  const std::string full = slurp("adsbfi_gilbert_35nm.json");
  CHECK(parse(full.substr(0, full.size() / 2)) == -2, "stream cut off mid-array -> -2 (no partial snapshot)");
  n = parse(full, 5);
  bool nearest = n == 5;
  const int all = parse(full);
  CHECK(nearest && all > 5, "cap keeps the NEAREST aircraft while streaming");
}

static RouteInfo route(const char *o, double olat, double olon, const char *d, double dlat, double dlon) {
  RouteInfo r{};
  r.hasRoute = true;
  strcpy(r.origIata, o);
  strcpy(r.destIata, d);
  r.oLat = olat; r.oLon = olon; r.dLat = dlat; r.dLon = dlon;
  return r;
}

void testPlausible() {
  std::printf("route plausibility (docs/04)\n");
  Aircraft a{};
  // The real case: SWA1637 descending westbound into PHX, adsbdb says PIT->TPA.
  a.lat = 33.4408; a.lon = -111.9567; a.track = 270.4f;
  CHECK(!routePlausible(route("PIT", 40.4915, -80.2329, "TPA", 27.9755, -82.5332), a),
        "SWA1637 near PHX 'PIT->TPA' is implausible");
  // Departing PHX for DEN, 10 nm out.
  a.lat = 33.55; a.lon = -111.90; a.track = 20;
  CHECK(routePlausible(route("PHX", 33.4343, -112.0116, "DEN", 39.8561, -104.6737), a),
        "PHX->DEN just after departure is plausible");
  // En route LAS->PHX over Wickenburg, heading SE.
  a.lat = 33.97; a.lon = -112.73; a.track = 135;
  CHECK(routePlausible(route("LAS", 36.0840, -115.1537, "PHX", 33.4343, -112.0116), a),
        "LAS->PHX en route is plausible");
  // Same position (~47 nm from PHX, so no endpoint exemption) flying AWAY from PHX.
  a.track = 315;
  CHECK(!routePlausible(route("LAS", 36.0840, -115.1537, "PHX", 33.4343, -112.0116), a),
        "flying away from the destination is implausible");
  RouteInfo none{};
  CHECK(!routePlausible(none, a), "no route -> not plausible");
}

#include "aircraft_names.h"

void testLabels() {
  std::printf("plane card labels (06 §4.1)\n");
  char op[28];
  shortOperator("SOUTHWEST AIRLINES CO", op, sizeof(op));
  CHECK(strcmp(op, "Southwest") == 0, "'SOUTHWEST AIRLINES CO' -> 'Southwest'");

  Aircraft a{};
  strcpy(a.hex, "a51f0f"); strcpy(a.callsign, "SWA1637"); strcpy(a.type, "B737"); strcpy(a.reg, "N429WN");
  RouteInfo r{};
  r.found = r.hasRoute = true;
  strcpy(r.airline, "Southwest Airlines"); strcpy(r.flightIata, "WN1637");
  PlaneLabels L;
  planeLabels(a, &r, L);
  CHECK(L.airline && strcmp(L.op, "Southwest") == 0 && strcmp(L.type, "737-700") == 0,
        "airliner: 'Southwest . 737-700'");
  CHECK(strcmp(L.line2a, "WN 1637") == 0 && strcmp(L.line2b, "N429WN") == 0, "line 2: 'WN 1637 . N429WN'");

  Aircraft g{};
  strcpy(g.hex, "a3c1f2"); strcpy(g.callsign, "N172SP"); strcpy(g.type, "C172"); strcpy(g.reg, "N172SP");
  planeLabels(g, nullptr, L);
  CHECK(!L.airline && strcmp(L.type, "Cessna 172") == 0, "GA: 'Cessna 172', no operator");
  CHECK(strcmp(L.line2a, "N172SP") == 0 && strcmp(L.line2b, "Private") == 0, "GA owner name never shown");
}
