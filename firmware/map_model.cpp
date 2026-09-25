#include "map_model.h"
#include "geo.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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

void mapAhead(const Aircraft &a, uint16_t secs, double &lat, double &lon) {
  const geo::LatLon f = geo::destination({a.lat, a.lon}, a.track < 0 ? 0 : a.track, a.gsKt * 0.514444 * secs);
  lat = f.lat;
  lon = f.lon;
}

bool mapWillPop(const Aircraft &a, WillPop &out) {
  out = {0, 0, 0};
  if (a.onGround || a.track < 0 || a.gsKt < 30 || mapQualifies(a.distNm, a.elDeg, a.altFt)) return false;
  for (uint8_t s = MAP_POP_STEP_S; s <= 60; s += MAP_POP_STEP_S) {
    double lat, lon;
    mapAhead(a, s, lat, lon);
    const double alt = a.altFt + a.vRateFpm * (s / 60.0);
    double d, brg;
    geo::distBearing({OBS_LAT, OBS_LON}, {lat, lon}, d, brg);
    if (mapQualifies(d / geo::M_PER_NM, geo::elevation(d, alt, OBS_ELEV_FT), alt)) {
      out = {s, lat, lon};
      return true;
    }
  }
  return false;
}

uint8_t mapWillPopSecs(const Aircraft &a) {
  WillPop w;
  return mapWillPop(a, w) ? w.secs : 0;
}

int mapPickFocus(const Traffic &t, const char *sel, const char *afterPop, FocusHold &hold, bool newPoll) {
  if (sel && sel[0])
    for (uint8_t i = 0; i < t.n; i++)
      if (strcmp(t.ac[i].hex, sel) == 0) return i;
  if (hold.hex[0]) {
    int held = -1;
    for (uint8_t i = 0; i < t.n; i++)
      if (strcmp(t.ac[i].hex, hold.hex) == 0) held = i;
    if (held >= 0) {
      if (mapWillPopSecs(t.ac[held])) hold.misses = 0;
      else if (newPoll) hold.misses++;
      if (hold.misses < 2) return held;
    }
    hold.hex[0] = 0;
    hold.misses = 0;
  }
  int best = -1;
  uint8_t bestS = 255;
  for (uint8_t i = 0; i < t.n; i++) {
    const Aircraft &a = t.ac[i];
    if (a.seenPos > MAX_SEEN_POS_S) continue;
    const uint8_t s = mapWillPopSecs(a);
    if (s && s < bestS) { bestS = s; best = i; }
  }
  if (best >= 0) {
    strncpy(hold.hex, t.ac[best].hex, sizeof(hold.hex) - 1);
    hold.hex[sizeof(hold.hex) - 1] = 0;
    hold.misses = 0;
    return best;
  }
  if (afterPop && afterPop[0])
    for (uint8_t i = 0; i < t.n; i++)
      if (strcmp(t.ac[i].hex, afterPop) == 0) return i;
  return -1;
}

bool mapIdleExpired(uint32_t now, uint32_t timerStart, uint32_t lastTouch, bool willPopFocus) {
  if ((int32_t)(now - lastTouch) > (int32_t)(MAP_IDLE_MAX_S * 1000UL)) return true;
  return !willPopFocus && (int32_t)(now - timerStart) > (int32_t)(MAP_IDLE_S * 1000UL);
}

int mapTap(const Traffic &t, float pxPerNm, int16_t x, int16_t y, uint32_t now, const char *focusHex, TapCycle &c,
           uint8_t &k, uint8_t &n) {
  k = n = 0;
  if (c.lastMs && (int32_t)(now - c.lastMs) < 250) return -2;   // panel bounce
  c.lastMs = now ? now : 1;
  auto indexOf = [&](const char *hex) {
    for (uint8_t i = 0; i < t.n; i++)
      if (strcmp(t.ac[i].hex, hex) == 0) return (int)i;
    return -1;
  };
  bool focusInList = false;
  for (uint8_t i = 0; i < c.n; i++) focusInList |= focusHex && strcmp(c.hex[i], focusHex) == 0;
  const bool atAnchor = hypotf((float)(x - c.ax), (float)(y - c.ay)) <= MAP_TAP_RADIUS_PX;
  const bool advance = c.n && focusInList && atAnchor;   // checked BEFORE the hit test: the
  if (!advance && mapHitTest(t, pxPerNm, x, y, MAP_TAP_RADIUS_PX) < 0) {   // planes may drift
    c.n = 0;
    return -1;
  }
  if (advance) {
    for (uint8_t step = 1; step <= c.n; step++) {                 // next frozen hex still in the traffic
      const uint8_t j = (c.idx + step) % c.n;
      if (indexOf(c.hex[j]) >= 0) {
        c.idx = j;
        break;
      }
    }
  } else {                                                          // a new list, frozen now
    int8_t cand[8];
    c.n = mapTapCandidates(t, pxPerNm, x, y, MAP_TAP_RADIUS_PX, cand, 8);
    for (uint8_t i = 0; i < c.n; i++) {
      strncpy(c.hex[i], t.ac[cand[i]].hex, 6);
      c.hex[i][6] = 0;
    }
    c.idx = 0;
    c.ax = x;
    c.ay = y;
  }
  for (uint8_t i = 0; i < c.n; i++) {                               // k of n, among those still present
    if (indexOf(c.hex[i]) < 0) continue;
    n++;
    if (i <= c.idx) k++;
  }
  return indexOf(c.hex[c.idx]);
}

void mapAltTag(int32_t ft, char *out, size_t n, bool unit) {
  if (ft < 1000) snprintf(out, n, "%ld%s", (long)(lroundf(ft / 100.0f) * 100), unit ? " ft" : "");
  else if (ft < 10000) snprintf(out, n, "%.1fk%s", ft / 1000.0f, unit ? " ft" : "");
  else snprintf(out, n, "%ldk%s", (long)lroundf(ft / 1000.0f), unit ? " ft" : "");
}

uint8_t mapStripRight(StripKind kind, const Aircraft &a, uint8_t k, uint8_t n, uint8_t popSecs, int16_t typeW,
                      int16_t opW, TextWidthFn width, StripSeg out[2], bool *preferred) {
  struct Opt { const char *t[2]; uint8_t pal[2]; uint8_t n; };   // 1-2 segments, docs/09 table
  char dist[16], a0[24], a1[24];
  snprintf(dist, sizeof(dist), "%.1f mi %s", a.distNm * 1.15078f, geo::compass8(a.azDeg));
  Opt opts[3];
  uint8_t no = 0;
  switch (kind) {
    case StripKind::Cycle:
      snprintf(a0, sizeof(a0), "%u of %u here", k, n);
      opts[no++] = {{a0, nullptr}, {M_TEXT, 0}, 1};
      break;
    case StripKind::WillPop:
      snprintf(a0, sizeof(a0), "overhead in ~%u s", popSecs);
      snprintf(a1, sizeof(a1), "in ~%u s", popSecs);
      opts[no++] = {{a0, dist}, {M_PLANE, M_MUTED}, 2};
      opts[no++] = {{a0, nullptr}, {M_PLANE, 0}, 1};
      opts[no++] = {{a1, nullptr}, {M_PLANE, 0}, 1};
      break;
    case StripKind::Passed:
      opts[no++] = {{"passed", dist}, {M_MUTED, M_MUTED}, 2};
      opts[no++] = {{"passed", nullptr}, {M_MUTED, 0}, 1};
      break;
    case StripKind::InDisc:                                  // Font 2 draws a backtick as a degree sign
      snprintf(a0, sizeof(a0), "%d` up", (int)lroundf(a.elDeg));
      snprintf(a1, sizeof(a1), "needs %d`", (int)ENTER_MIN_ELEV_DEG);
      opts[no++] = {{a0, a1}, {M_MUTED, M_MUTED}, 2};
      opts[no++] = {{a0, nullptr}, {M_MUTED, 0}, 1};
      break;
    case StripKind::Default:
      mapAltTag(a.altFt, a0, sizeof(a0), true);
      opts[no++] = {{a0, dist}, {M_MUTED, M_MUTED}, 2};
      break;
  }
  auto optW = [&](const Opt &o) {
    int16_t w = 0;
    for (uint8_t i = 0; i < o.n; i++) w += width(o.t[i]) + (i ? 10 : 0);
    return w;
  };
  int pick = -1;
  for (uint8_t i = 0; i < no && pick < 0; i++)
    if (300 - optW(opts[i]) - 8 - 26 >= typeW + 10 + opW) pick = i;        // the operator whole
  for (uint8_t i = 0; i < no && pick < 0; i++)
    if (300 - optW(opts[i]) - 8 - 26 >= typeW + 10 + 40) pick = i;         // a 40 px stub
  if (pick < 0) pick = no - 1;
  if (preferred) *preferred = pick == 0;
  for (uint8_t i = 0; i < opts[pick].n; i++) {
    strncpy(out[i].text, opts[pick].t[i], sizeof(out[i].text) - 1);
    out[i].text[sizeof(out[i].text) - 1] = 0;
    out[i].pal = opts[pick].pal[i];
  }
  return opts[pick].n;
}

uint8_t mapTapCandidates(const Traffic &t, float pxPerNm, int16_t x, int16_t y, int16_t radiusPx,
                         int8_t *out, uint8_t cap) {
  float dist[MAX_AIRCRAFT];
  int8_t all[MAX_AIRCRAFT];
  uint8_t n = 0;
  for (uint8_t i = 0; i < t.n && n < MAX_AIRCRAFT; i++) {
    if (t.ac[i].onGround) continue;
    float ax, ay;
    mapProject(t.ac[i].lat, t.ac[i].lon, pxPerNm, ax, ay);
    const float d = hypotf(ax - x, ay - y);
    if (d > radiusPx) continue;
    uint8_t k = n++;                                   // insertion sort by distance from the tap
    while (k > 0 && dist[k - 1] > d) { dist[k] = dist[k - 1]; all[k] = all[k - 1]; k--; }
    dist[k] = d;
    all[k] = (int8_t)i;
  }
  if (n > cap) n = cap;                                // the `cap` NEAREST to the tap
  memcpy(out, all, n);
  return n;
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
