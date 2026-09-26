#include "trails_log.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

namespace {

void put16(uint8_t *p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (v >> (8 * i)) & 0xFF; }
uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

bool parseHex(const char *hex, uint32_t &out) {
  if (!hex || strlen(hex) != 6) return false;
  char *end;
  out = (uint32_t)strtoul(hex, &end, 16);
  return *end == '\0';
}

bool quant(double deg, int16_t &out) {           // false when out of range: skipped, never clamped
  const double q = deg / TRAILS_DEG_UNIT;
  if (!(q >= -32767 && q <= 32767)) return false;
  out = (int16_t)lround(q);
  return true;
}

}  // namespace

bool trailsKeep(const Aircraft &a) {
  uint32_t icao;
  if (a.hex[0] == '~' || !parseHex(a.hex, icao)) return false;     // TIS-B / non-ICAO: never drawn
  if (a.onGround || a.seenPos > MAX_SEEN_POS_S) return false;     // only fresh, airborne positions
  if (!(a.lat >= -90 && a.lat <= 90 && a.lon >= -180 && a.lon <= 180)) return false;
  return true;
}

void trailsHeader(uint32_t dayKey, double buildLat, double buildLon, uint8_t out[TRAILS_HDR_BYTES]) {
  memcpy(out, TRAILS_MAGIC, 4);
  put32(out + 4, dayKey);
  put32(out + 8, (uint32_t)(int32_t)lround(buildLat * 1e6));
  put32(out + 12, (uint32_t)(int32_t)lround(buildLon * 1e6));
}

size_t trailsEncodePoll(const Aircraft *ac, uint8_t n, uint32_t secOfDay, double buildLat, double buildLon,
                        uint8_t *out, size_t cap) {
  if (cap < 4) return 0;
  size_t len = 4;
  uint8_t kept = 0;
  for (uint8_t i = 0; i < n && kept < 255 && len + TRAILS_POINT_BYTES <= cap; i++) {
    const Aircraft &a = ac[i];
    if (!trailsKeep(a)) continue;
    uint32_t icao;
    int16_t dx, dy;
    parseHex(a.hex, icao);
    if (!quant(a.lon - buildLon, dx) || !quant(a.lat - buildLat, dy)) continue;   // > ~16 deg away
    uint8_t *p = out + len;
    p[0] = icao >> 16;
    p[1] = (icao >> 8) & 0xFF;
    p[2] = icao & 0xFF;
    put16(p + 3, (uint16_t)dx);
    put16(p + 5, (uint16_t)dy);
    const int32_t alt = a.altFt < 0 ? 0 : a.altFt / TRAILS_ALT_UNIT_FT;
    p[7] = (uint8_t)(alt > 255 ? 255 : alt);
    len += TRAILS_POINT_BYTES;
    kept++;
  }
  if (!kept) return 0;
  out[0] = TRAILS_REC_MARK;
  out[1] = kept;
  put16(out + 2, (uint16_t)((secOfDay % 86400) / 2));
  return len;
}

bool trailsNextRec(const uint8_t *buf, size_t len, size_t &off, TrailsRec &out) {
  if (off + 4 > len || buf[off] != TRAILS_REC_MARK) return false;
  const uint8_t n = buf[off + 1];
  const size_t need = 4 + (size_t)n * TRAILS_POINT_BYTES;
  if (!n || off + need > len) return false;
  out.secOfDay = (uint32_t)get16(buf + off + 2) * 2;
  if (out.secOfDay >= 86400) return false;
  out.n = n;
  out.points = buf + off + 4;
  off += need;
  return true;
}

TrailsPoint trailsPoint(const TrailsRec &r, uint8_t i, double buildLat, double buildLon) {
  const uint8_t *p = r.points + (size_t)i * TRAILS_POINT_BYTES;
  TrailsPoint t;
  t.icao = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
  t.lon = buildLon + (int16_t)get16(p + 3) * TRAILS_DEG_UNIT;
  t.lat = buildLat + (int16_t)get16(p + 5) * TRAILS_DEG_UNIT;
  t.altFt = (int32_t)p[7] * TRAILS_ALT_UNIT_FT;
  return t;
}
