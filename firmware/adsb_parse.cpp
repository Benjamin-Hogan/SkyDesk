// readsb JSON (adsb.fi / adsb.lol) -> Aircraft[]. No I/O: host-testable.
//
// STREAMED, one aircraft object at a time: memory use is constant no matter how
// many aircraft the radius holds. (Parsing the whole document ran out of heap
// on-device at an 18 nm radius while a TLS session was open: "JSON NoMemory".)
#include "adsb_parse.h"
#include "geo.h"

#include <algorithm>

namespace {

void copyTrim(char *dst, size_t n, const char *src) {
  if (!src) { dst[0] = '\0'; return; }
  while (*src == ' ') src++;
  strncpy(dst, src, n - 1);
  dst[n - 1] = '\0';
  for (int i = (int)strlen(dst) - 1; i >= 0 && dst[i] == ' '; --i) dst[i] = '\0';
}

}  // namespace

namespace {

// Filter for ONE aircraft object.
void buildFilter(JsonDocument &f) {
  static const char *fields[] = {"hex", "flight", "r", "t", "desc", "ownOp", "alt_baro",
                                 "alt_geom", "gs", "track", "baro_rate", "geom_rate",
                                 "lat", "lon", "seen_pos", "category"};
  for (const char *k : fields) f[k] = true;
}

// Advance past '"aircraft":[' (adsb.fi) or '"ac":[' (adsb.lol). Whitespace-tolerant.
bool seekAircraftArray(ByteSource &in) {
  char win[16] = {0};   // last non-space chars
  for (;;) {
    const int c = in.read();
    if (c < 0) return false;
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
    if (c == '[') {
      const size_t L = strlen(win);
      auto endsWith = [&](const char *s) {
        const size_t n = strlen(s);
        return L >= n && strcmp(win + L - n, s) == 0;
      };
      if (endsWith("\"aircraft\":") || endsWith("\"ac\":")) return true;
    }
    const size_t L = strlen(win);
    if (L == sizeof(win) - 1) memmove(win, win + 1, L);   // keep the tail
    win[strlen(win)] = (char)c;
    win[sizeof(win) - 1] = '\0';
  }
}

}  // namespace

namespace {

// Parse one readsb aircraft object. Returns false if it has no usable position.
bool parseOne(JsonObjectConst o, Aircraft &a) {
  if (o["lat"].isNull() || o["lon"].isNull()) return false;
  memset(&a, 0, sizeof(a));
  copyTrim(a.hex, sizeof(a.hex), o["hex"] | "");
  if (!a.hex[0]) return false;
  copyTrim(a.callsign, sizeof(a.callsign), o["flight"] | "");
  copyTrim(a.reg, sizeof(a.reg), o["r"] | "");
  copyTrim(a.type, sizeof(a.type), o["t"] | "");
  copyTrim(a.desc, sizeof(a.desc), o["desc"] | "");
  copyTrim(a.ownOp, sizeof(a.ownOp), o["ownOp"] | "");
  copyTrim(a.category, sizeof(a.category), o["category"] | "");
  a.lat = o["lat"].as<double>();
  a.lon = o["lon"].as<double>();

  JsonVariantConst baro = o["alt_baro"];
  if (baro.is<const char *>()) {           // "ground"
    a.onGround = true;
    a.altFt = 0;
  } else {
    a.altFt = !o["alt_geom"].isNull() ? o["alt_geom"].as<int32_t>() : baro.as<int32_t>();
  }
  a.gsKt = o["gs"] | 0.0f;
  a.track = o["track"] | -1.0f;
  a.vRateFpm = !o["geom_rate"].isNull() ? o["geom_rate"].as<int16_t>() : (int16_t)(o["baro_rate"] | 0);
  a.seenPos = o["seen_pos"] | 0.0f;

  double d, az;
  geo::distBearing({OBS_LAT, OBS_LON}, {a.lat, a.lon}, d, az);
  a.distNm = d / geo::M_PER_NM;
  a.azDeg = az;
  a.elDeg = a.onGround ? -10 : geo::elevation(d, a.altFt, OBS_ELEV_FT);
  return true;
}

}  // namespace

int adsbParseStream(ByteSource &in, Aircraft *out, size_t cap) {
  // adsb.fi uses "aircraft", adsb.lol uses "ac" (docs/04 §1).
  if (!seekAircraftArray(in)) return -1;
  JsonDocument filter;
  buildFilter(filter);
  JsonDocument doc;            // holds ONE aircraft at a time
  size_t n = 0;
  Aircraft a;
  for (;;) {
    int c;
    while ((c = in.peek()) == ' ' || c == ',' || c == '\n' || c == '\r' || c == '\t') in.read();
    if (c == ']') break;                          // end of the array
    if (c != '{') return -2;                      // truncated / timed out
    const DeserializationError err = deserializeJson(doc, in, DeserializationOption::Filter(filter));
    if (err) return -2;                           // a partial snapshot would fake "lost" planes
    // Ground traffic is useless everywhere (tracker, chip, map) and at PHX it is
    // most of the feed - drop it BEFORE any truncation (docs/08 -> Data).
    if (!parseOne(doc.as<JsonObjectConst>(), a) || a.onGround) continue;
    if (n < cap) {
      out[n++] = a;
    } else {                                   // full: keep the nearer one
      size_t far = 0;
      for (size_t i = 1; i < n; i++)
        if (out[i].distNm > out[far].distNm) far = i;
      if (a.distNm < out[far].distNm) out[far] = a;
    }
  }
  std::sort(out, out + n, [](const Aircraft &x, const Aircraft &y) { return x.distNm < y.distNm; });
  return (int)n;
}

size_t adsbKeepNearest(Aircraft *buf, size_t n, size_t keep, const char *pinned) {
  if (n <= keep) return n;
  if (pinned && pinned[0]) {
    for (size_t i = keep; i < n; i++) {
      if (strcmp(buf[i].hex, pinned) == 0) {   // never evict the selected plane
        buf[keep - 1] = buf[i];
        break;
      }
    }
  }
  return keep;
}
