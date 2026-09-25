// Plane map screen - layout from docs/mockups/map_screen.py; rules in
// docs/08-plane-map.md. Rendered in 5 horizontal bands through one 320x48
// 4-bit sprite (08 -> "Rendering: band sprite"). NO anti-aliased calls here.
#include "aircraft_names.h"
#include "geo.h"
#include "map_model.h"
#include "route_client.h"
#include "settings.h"
#include "tracker.h"
#include "ui_internal.h"

#include <math.h>

using namespace ui;

namespace {

constexpr int16_t BAND_H = 48;
constexpr int16_t STRIP_Y = 214;
constexpr int16_t RING_PX = 100;          // range ring radius == zoom chip distance
const uint8_t ZOOM_MI[MAP_ZOOM_N] = {5, 10, 20};

struct Rect { int16_t x0, y0, x1, y1; };
bool overlaps(const Rect &a, const Rect &b) {
  return !(a.x1 <= b.x0 || a.x0 >= b.x1 || a.y1 <= b.y0 || a.y0 >= b.y1);
}

// ---- per-frame layout, computed once and replayed into every band ---------
enum class Look : uint8_t { Normal, Focus, Inbound, Dim };
struct PlaneDraw {
  int8_t idx; float x, y; Look look; uint8_t members; bool tag; int16_t tagX; bool backed;
};
struct LabelDraw { const char *text; int16_t x, y; bool town; };

PlaneDraw g_planes[MAX_AIRCRAFT];
uint8_t   g_nPlanes = 0;
LabelDraw g_labels[48];
uint8_t   g_nLabels = 0;
Rect      g_taken[MAX_AIRCRAFT * 2 + 64];
uint8_t   g_nTaken = 0;
int8_t    g_focus = -1;               // index into g_planes
float     g_truncNm = 0;              // >0: only the nearest 40 are shown, ring at this distance
int8_t    g_nearestOut = -1;          // empty state: nearest aircraft beyond the view

enum class MapState : uint8_t { Live, Loading, OfflineRecent, OfflineCleared };
MapState  g_state = MapState::Live;
uint8_t   g_coveredNm = POLL_RADIUS_NM;   // radius the current snapshot covers
uint8_t   g_wantNm = POLL_RADIUS_NM;
char      g_selected[7] = "";
uint32_t  g_trafficVer = UINT32_MAX;
uint32_t  g_lastDraw = 0;
bool      g_dirty = true;

uint8_t zoom() { return settings().mapZoom; }
float ppn() { return MAP_ZOOMS[zoom()].pxPerNm; }

bool isFree(const Rect &r) {
  if (r.x0 < 0 || r.x1 > MAP_W || r.y0 < 0) return false;
  for (uint8_t i = 0; i < g_nTaken; i++)
    if (overlaps(r, g_taken[i])) return false;
  return true;
}
void take(const Rect &r) {
  if (g_nTaken < sizeof(g_taken) / sizeof(g_taken[0])) g_taken[g_nTaken++] = r;
}

const char *callsignOf(const Aircraft &a) { return a.callsign[0] ? a.callsign : (a.reg[0] ? a.reg : a.hex); }

// "Will pop soon" lives in map_model.cpp (host-tested): mapWillPopSecs().

void layout(const Traffic &t) {
  const float k = ppn();
  const bool live = g_state == MapState::Live || g_state == MapState::Loading;
  g_nPlanes = g_nLabels = g_nTaken = 0;
  g_focus = -1;
  g_nearestOut = -1;
  g_truncNm = (live && t.totalInRadius > t.n && t.n) ? t.ac[t.n - 1].distNm : 0;

  // chrome, OSM credit, strip, you
  take({0, 0, 48, 36});
  take({246, 0, MAP_W, 36});
  take({146, 0, 174, 30});
  take({0, STRIP_Y - 12, 60, MAP_H});
  take({0, STRIP_Y - 2, MAP_W, MAP_H});
  take({MAP_CX - 7, MAP_CY - 7, MAP_CX + 8, MAP_CY + 8});

  if (g_state != MapState::OfflineCleared) {
    // project (list is nearest-first; ground traffic never reaches the UI)
    for (uint8_t i = 0; i < t.n && g_nPlanes < MAX_AIRCRAFT; i++) {
      const Aircraft &a = t.ac[i];
      if (g_state == MapState::Loading && a.distNm > g_coveredNm) continue;
      float x, y;
      mapProject(a.lat, a.lon, k, x, y);
      if (x < -10 || x > MAP_W + 10 || y < -10 || y > STRIP_Y - 4) {
        if (g_nearestOut < 0) g_nearestOut = i;   // list is nearest-first
        continue;
      }
      g_planes[g_nPlanes++] = {(int8_t)i, x, y, Look::Normal, 1, false, 0, false};
    }
    // focus: selected, else nearest
    for (uint8_t p = 0; p < g_nPlanes; p++)
      if (g_selected[0] && strcmp(t.ac[g_planes[p].idx].hex, g_selected) == 0) g_focus = p;
    if (g_focus < 0 && g_nPlanes) g_focus = 0;

    // cluster at the widest zoom: the focus plane never joins a cluster or hosts one.
    // Compaction is in place, so track the focus by both its old and new index.
    if (zoom() == 2) {
      const int8_t focusOld = g_focus;
      int8_t focusNew = -1;
      uint8_t w = 0;
      for (uint8_t p = 0; p < g_nPlanes; p++) {
        int8_t host = -1;
        if (p != focusOld) {
          for (uint8_t q = 0; q < w; q++) {
            if (q == focusNew) continue;
            if (hypotf(g_planes[q].x - g_planes[p].x, g_planes[q].y - g_planes[p].y) < MAP_CLUSTER_PX) {
              host = q;
              break;
            }
          }
        }
        if (host >= 0) { g_planes[host].members++; continue; }
        if (p == focusOld) focusNew = w;
        g_planes[w++] = g_planes[p];
      }
      g_nPlanes = w;
      g_focus = focusNew;
    }

    for (uint8_t p = 0; p < g_nPlanes; p++) {
      PlaneDraw &d = g_planes[p];
      const Aircraft &a = t.ac[d.idx];
      if (!live || a.seenPos > MAX_SEEN_POS_S) d.look = Look::Dim;
      else if (p == g_focus) d.look = Look::Focus;
      else if (mapWillPopSecs(a)) d.look = Look::Inbound;   // dismissed planes stay white
      take({(int16_t)(d.x - 8), (int16_t)(d.y - 8), (int16_t)(d.x + 8), (int16_t)(d.y + 8)});
    }
  }

  // airports before tags
  setFont(*band, Font::Glcd);
  for (uint8_t i = 0; i < MAP_AIRPORT_N && g_nLabels < 48; i++) {
    float x, y;
    mapProject(MAP_AIRPORTS[i].lat, MAP_AIRPORTS[i].lon, k, x, y);
    const int16_t w = band->textWidth(MAP_AIRPORTS[i].name);
    const Rect r{(int16_t)(x + 5), (int16_t)(y - 4), (int16_t)(x + 6 + w), (int16_t)(y + 5)};
    if (isFree(r)) {
      take(r);
      g_labels[g_nLabels++] = {MAP_AIRPORTS[i].name, (int16_t)(x + 5), (int16_t)(y + 4), false};
    }
  }

  // tags: focus first, then by distance; right, else left
  if (live) {
    for (int pass = 0; pass < 2; pass++) {
      for (uint8_t p = 0; p < g_nPlanes; p++) {
        PlaneDraw &d = g_planes[p];
        if ((pass == 0) != (p == g_focus) || d.members > 1 || d.look == Look::Dim) continue;
        const int16_t w = max<int16_t>(band->textWidth(callsignOf(t.ac[d.idx])), 18);
        const int16_t gap = p == g_focus ? 14 : 10;   // clear of the focus ring
        for (int16_t x0 : {(int16_t)(d.x + gap), (int16_t)(d.x - gap - w)}) {
          const Rect r{(int16_t)(x0 - 1), (int16_t)(d.y - 9), (int16_t)(x0 + w + 1), (int16_t)(d.y + 9)};
          if (isFree(r)) { take(r); d.tag = true; d.tagX = x0; break; }
        }
        if (!d.tag && p == g_focus) {   // the focus tag always lands: vs chrome only, on a backing
          const uint8_t saved = g_nTaken;
          g_nTaken = 6;                 // the first 6 rects are chrome/strip/you
          int bestCover = 1000;
          for (int16_t x0 : {(int16_t)(d.x + gap), (int16_t)(d.x - gap - w)}) {
            const Rect r{(int16_t)(x0 - 2), (int16_t)(d.y - 10), (int16_t)(x0 + w + 2), (int16_t)(d.y + 10)};
            if (!isFree(r)) continue;
            int cover = 0;   // a backing hides planes (and hidden planes can't be tapped)
            for (uint8_t q = 0; q < g_nPlanes; q++)
              cover += overlaps(r, {(int16_t)(g_planes[q].x - 6), (int16_t)(g_planes[q].y - 6),
                                    (int16_t)(g_planes[q].x + 6), (int16_t)(g_planes[q].y + 6)});
            if (cover < bestCover) { bestCover = cover; d.tag = d.backed = true; d.tagX = x0; }
          }
          g_nTaken = saved;
          if (d.backed) take({(int16_t)(d.tagX - 2), (int16_t)(d.y - 10), (int16_t)(d.tagX + w + 2), (int16_t)(d.y + 10)});
        }
      }
    }
  }

  // towns only where free; your own town is skipped
  setFont(*band, Font::F2);
  for (uint8_t i = 0; i < MAP_TOWN_N && g_nLabels < 48; i++) {
    const MapLabel &m = MAP_TOWNS[i];
    if (!m.major && zoom() == 2) continue;
    float x, y;
    mapProject(m.lat, m.lon, k, x, y);
    if (hypotf(x - MAP_CX, y - MAP_CY) < 30) continue;
    const int16_t w = band->textWidth(m.name);
    const Rect r{(int16_t)(x - w / 2), (int16_t)(y - 12), (int16_t)(x + w / 2), (int16_t)(y + 3)};
    if (y < STRIP_Y - 4 && isFree(r)) {
      take(r);
      g_labels[g_nLabels++] = {m.name, (int16_t)x, (int16_t)y, true};
    }
  }
}

// Replace background pixels inside the 3 nm disc with the zone tint.
void tintZone(uint8_t *buf, int16_t y0, float zr) {
  for (int16_t row = 0; row < BAND_H; row++) {
    const float dy = (y0 + row) - MAP_CY;
    if (fabsf(dy) > zr) continue;
    const float half = sqrtf(zr * zr - dy * dy);
    const int16_t xa = max<int16_t>(0, (int16_t)(MAP_CX - half)), xb = min<int16_t>(MAP_W - 1, (int16_t)(MAP_CX + half));
    uint8_t *line = buf + row * (MAP_W / 2);
    for (int16_t x = xa; x <= xb; x++) {
      uint8_t &b = line[x >> 1];
      if (x & 1) { if ((b & 0x0F) == M_BG) b = (b & 0xF0) | M_ZONE; }
      else if ((b >> 4) == M_BG) b = (b & 0x0F) | (M_ZONE << 4);
    }
  }
}

void dottedCircle(TFT_eSprite &s, int16_t cy, float r, uint8_t c, float step) {
  const int n = (int)(2 * PI * r / step);
  for (int i = 0; i < n; i++) {
    const float a = 2 * PI * i / n;
    const int16_t x = MAP_CX + lroundf(r * cosf(a)), y = cy + lroundf(r * sinf(a));
    if (y + (MAP_CY - cy) < STRIP_Y) s.drawPixel(x, y, c);
  }
}

// Chevrons from fillTriangles - no anti-aliased lines in a 4-bit sprite.
void chevronLeft(TFT_eSprite &s, int16_t x, int16_t y, uint8_t c) {
  s.fillTriangle(x + 7, y - 7, x + 7, y - 4, x, y, c);
  s.fillTriangle(x + 7, y - 4, x + 3, y, x, y, c);
  s.fillTriangle(x + 7, y + 7, x + 7, y + 4, x, y, c);
  s.fillTriangle(x + 7, y + 4, x + 3, y, x, y, c);
}
void chevronRight(TFT_eSprite &s, int16_t x, int16_t y, uint8_t c) {
  s.fillTriangle(x, y - 5, x, y - 2, x + 5, y, c);
  s.fillTriangle(x, y - 2, x + 2, y, x + 5, y, c);
  s.fillTriangle(x, y + 5, x, y + 2, x + 5, y, c);
  s.fillTriangle(x, y + 2, x + 2, y, x + 5, y, c);
}

void altTag(int32_t ft, char *out, size_t n, bool unit) {
  if (ft < 1000) snprintf(out, n, "%ld%s", (long)(lroundf(ft / 100.0f) * 100), unit ? " ft" : "");
  else if (ft < 10000) snprintf(out, n, "%.1fk%s", ft / 1000.0f, unit ? " ft" : "");
  else snprintf(out, n, "%ldk%s", (long)lroundf(ft / 1000.0f), unit ? " ft" : "");
}

void drawBand(TFT_eSprite &s, int16_t y0, const Traffic &t) {
  const float k = ppn();
  const int16_t cy = MAP_CY - y0;   // everything is drawn shifted by -y0; the sprite clips
  const bool live = g_state == MapState::Live || g_state == MapState::Loading;

  memcpy(s.getPointer(), MAP_ZOOMS[zoom()].img + (size_t)y0 * (MAP_W / 2), BAND_H * (MAP_W / 2));
  tintZone((uint8_t *)s.getPointer(), y0, ENTER_RADIUS_NM * k);

  s.drawCircle(MAP_CX, cy, lroundf(ENTER_RADIUS_NM * k), M_PLANE_DIM);
  dottedCircle(s, cy, EXIT_RADIUS_NM * k, M_DIM, 4);
  dottedCircle(s, cy, RING_PX, M_DIM, 7);
  if (g_state == MapState::Loading) {
    dottedCircle(s, cy, g_coveredNm * k, M_MUTED, 3);
    const float off = g_coveredNm * k * 0.72f;
    drawText(s, "WIDENING...", MAP_CX + lroundf(off), cy + lroundf(off) + 10, Font::Glcd, M_MUTED);
  } else if (g_truncNm > 0) {
    dottedCircle(s, cy, g_truncNm * k, M_MUTED, 3);   // beyond this: not shown, not empty
  }

  for (uint8_t i = 0; i < g_nLabels; i++) {
    const LabelDraw &l = g_labels[i];
    if (l.town) drawText(s, l.text, l.x, l.y - y0, Font::F2, M_DIM, C_BASELINE);
    else drawText(s, l.text, l.x, l.y - y0, Font::Glcd, M_MUTED);
  }

  if (live) {   // trails: real history
    for (uint8_t p = 0; p < g_nPlanes; p++) {
      const Trail *tr = trailFor(t.ac[g_planes[p].idx].hex);
      if (!tr) continue;
      for (uint8_t j = 1; j < tr->n; j++) {   // skip the newest point (under the glyph)
        const uint8_t at = (tr->head + MAP_TRAIL_N - j) % MAP_TRAIL_N;
        float x, y;
        mapProject(tr->lat[at], tr->lon[at], k, x, y);
        if (y < STRIP_Y) s.drawPixel(lroundf(x), lroundf(y) - y0, M_TRAIL);
      }
    }
  }

  const float scale = zoom() == 2 ? 0.45f : 0.6f;
  for (uint8_t p = 0; p < g_nPlanes; p++) {
    const PlaneDraw &d = g_planes[p];
    const Aircraft &a = t.ac[d.idx];
    const uint8_t c = d.look == Look::Focus ? M_PLANE : d.look == Look::Inbound ? M_PLANE_DIM
                    : d.look == Look::Dim ? M_DIM : M_TEXT;
    if (d.look == Look::Focus) s.drawCircle(lroundf(d.x), lroundf(d.y) - y0, 11, M_PLANE);
    drawPlaneGlyph(s, d.x, d.y - y0, a.track >= 0 ? a.track : 0, scale, c);
    if (d.members > 1) {
      char n[4];
      snprintf(n, sizeof(n), "%d", d.members);
      setFont(s, Font::Glcd);
      const int16_t bw = s.textWidth(n) + 4;
      int16_t bx = lroundf(d.x) + 5;
      if (g_focus >= 0 && hypotf(g_planes[g_focus].x - (bx + bw / 2), g_planes[g_focus].y - (d.y - 7)) < 16)
        bx = lroundf(d.x) - 5 - bw;   // keep clear of the focus ring
      s.fillRect(bx, lroundf(d.y) - 12 - y0, bw, 10, M_PANEL);
      drawText(s, n, bx + 2, lroundf(d.y) - 4 - y0, Font::Glcd, M_TEXT);
    }
    if (d.tag) {
      char alt[10];
      altTag(a.altFt, alt, sizeof(alt), false);
      if (d.backed) {
        setFont(s, Font::Glcd);
        const int16_t w = max<int16_t>(s.textWidth(callsignOf(a)), s.textWidth(alt));
        s.fillRect(d.tagX - 2, lroundf(d.y) - 10 - y0, w + 4, 20, M_PANEL);
      }
      drawText(s, callsignOf(a), d.tagX, lroundf(d.y) - 1 - y0, Font::Glcd, d.look == Look::Focus ? M_PLANE : M_TEXT);
      drawText(s, alt, d.tagX, lroundf(d.y) + 8 - y0, Font::Glcd, M_MUTED);
    }
  }

  s.fillCircle(MAP_CX, cy, 5, M_TEXT);   // you
  s.fillCircle(MAP_CX, cy, 3, M_YOU);

  // chrome
  s.fillRoundRect(4, 4 - y0, 36, 24, 12, M_PANEL);
  chevronLeft(s, 17, 16 - y0, M_TEXT);
  char lab[8];
  snprintf(lab, sizeof(lab), "%d MI", ZOOM_MI[zoom()]);
  setFont(s, Font::F2);
  const int16_t zw = s.textWidth(lab) + 18;
  s.fillRoundRect(316 - zw, 4 - y0, zw, 24, 12, M_PANEL);
  drawText(s, lab, 316 - zw / 2, 21 - y0, Font::F2, M_TEXT, C_BASELINE);
  s.fillTriangle(160, 3 - y0, 155, 12 - y0, 165, 12 - y0, M_MUTED);
  drawText(s, "N", 160, 24 - y0, Font::Glcd, M_MUTED, C_BASELINE);
  drawText(s, "(c) OSM", 4, STRIP_Y - 4 - y0, Font::Glcd, M_DIM);
}

void drawStrip(TFT_eSprite &s, int16_t y0, const Traffic &t) {
  if (y0 + BAND_H <= STRIP_Y) return;
  const int16_t oy = -y0, base = 231 + oy;
  s.fillRect(0, STRIP_Y + oy, MAP_W, MAP_H - STRIP_Y, M_PANEL);
  char buf[40];
  switch (g_state) {
    case MapState::OfflineRecent: {
      drawText(s, "Radar offline", 10, base, Font::F2, M_WARN);
      snprintf(buf, sizeof(buf), "positions %lu s old", (unsigned long)((millis() - t.fetchedMs) / 1000));
      drawText(s, buf, 310, base, Font::F2, M_MUTED, R_BASELINE);
      return;
    }
    case MapState::OfflineCleared:
      drawText(s, "No live traffic", 10, base, Font::F2, M_WARN);
      snprintf(buf, sizeof(buf), "retrying in %d s", (int)max<int32_t>(0, (int32_t)(t.nextRetryMs - millis()) / 1000));
      drawText(s, buf, 310, base, Font::F2, M_MUTED, R_BASELINE);
      return;
    case MapState::Loading:   // the strip keeps describing the focus plane
    case MapState::Live:
      break;
  }
  if (g_focus < 0) {
    snprintf(buf, sizeof(buf), "Nothing within %d mi", ZOOM_MI[zoom()]);
    drawText(s, buf, 10, base, Font::F2, M_MUTED);
    if (g_nearestOut >= 0) {
      const Aircraft &n = t.ac[g_nearestOut];
      snprintf(buf, sizeof(buf), "nearest %.1f mi %s", n.distNm * 1.15078f, geo::compass8(n.azDeg));
      drawText(s, buf, 310, base, Font::F2, M_MUTED, R_BASELINE);
    }
    return;
  }
  const Aircraft &a = t.ac[g_planes[g_focus].idx];
  RouteInfo r;
  const bool haveRoute = routeGet(a.hex, r);
  PlaneLabels L;
  planeLabels(a, haveRoute ? &r : nullptr, L);

  drawPlaneGlyph(s, 14, 227 + oy, a.track >= 0 ? a.track : 0, 0.5f, M_PLANE);
  // right side is fixed: elevation inside the disc (the real trigger, worded like the
  // card; Font 2's ` is a degree sign), altitude otherwise
  char dist[20], right[16];
  snprintf(dist, sizeof(dist), "%.1f mi %s", a.distNm * 1.15078f, geo::compass8(a.azDeg));
  const uint8_t popIn = mapWillPopSecs(a);
  if (popIn) snprintf(right, sizeof(right), "overhead in ~%d s", popIn);   // teaches dim amber
  else if (a.distNm <= ENTER_RADIUS_NM) snprintf(right, sizeof(right), "%d` up", (int)lroundf(a.elDeg));
  else altTag(a.altFt, right, sizeof(right), true);
  const int16_t rw = drawText(s, dist, 300, base, Font::F2, M_MUTED, R_BASELINE);
  drawSep(s, 300 - rw - 5, 226 + oy, M_DIM);
  const int16_t aw = drawText(s, right, 300 - rw - 10, base, Font::F2, M_MUTED, R_BASELINE);
  const int16_t avail = 300 - rw - 10 - aw - 8 - 26;

  // left: "<operator> . <type>" - type before flight number (v1 priority). Add the
  // flight number only if it fits; if still too wide truncate the OPERATOR, never the type.
  const char *type = L.type[0] ? L.type : "Unknown type";
  char op[32];
  if (L.airline) {
    const char *num = a.callsign;
    while (*num && !isdigit((unsigned char)*num)) num++;
    snprintf(op, sizeof(op), "%s %s", L.op, num);
    setFont(s, Font::F2);
    if (!*num || s.textWidth(op) + 10 + s.textWidth(type) > avail) snprintf(op, sizeof(op), "%s", L.op);
  } else {
    snprintf(op, sizeof(op), "%s", callsignOf(a));
  }
  setFont(s, Font::F2);
  const int16_t tw = s.textWidth(type);
  const Font f2 = Font::F2;
  const int16_t w = drawFit(s, op, 26, base, avail - 10 - tw, M_TEXT, &f2, 1);
  drawSep(s, 26 + w + 5, 226 + oy, M_DIM);
  drawText(s, type, 26 + w + 10, base, Font::F2, M_TEXT);
  chevronRight(s, 308, 226 + oy, M_MUTED);
}

void applyPollPlan() {
  const float zoomNm = RING_PX / ppn();
  g_wantNm = (uint8_t)min<float>(MAP_POLL_MAX_NM, ceilf(zoomNm * MAP_POLL_SCALE));
  g_wantNm = max<uint8_t>(g_wantNm, POLL_RADIUS_NM);
  appSetPollPlan(g_wantNm, zoom() == 0 ? MAP_POLL_Z0_MS : ADSB_POLL_WEATHER_MS);
}

}  // namespace

void mapEnter() {
  g_dirty = true;
  g_trafficVer = UINT32_MAX;
  uint8_t r;
  uint16_t i;
  appGetPollPlan(r, i);
  g_coveredNm = r;          // what the current snapshot was fetched with
  applyPollPlan();
}

void mapLeave() {
  appSetPollPlan(POLL_RADIUS_NM, ADSB_POLL_WEATHER_MS);
  g_selected[0] = '\0';
  appSetPinnedHex("");
}

void mapUpdate(const Traffic &t, bool radarUp, uint32_t now) {
  if (t.version != g_trafficVer) {
    g_trafficVer = t.version;
    if (t.ok) g_coveredNm = g_wantNm;   // a fetch after the plan change covers the new radius
    // the selection sticks until its plane leaves the traffic
    if (g_selected[0]) {
      bool present = false;
      for (uint8_t i = 0; i < t.n; i++) present |= strcmp(t.ac[i].hex, g_selected) == 0;
      if (!present) { g_selected[0] = '\0'; appSetPinnedHex(""); }
    }
    g_dirty = true;
  }

  // state: offline (recent / cleared), loading, live
  const uint32_t age = now - t.fetchedMs;
  MapState st = MapState::Live;
  if (!radarUp) st = age <= MAP_OFFLINE_KEEP_S * 1000UL ? MapState::OfflineRecent : MapState::OfflineCleared;
  else if (g_coveredNm < g_wantNm) st = MapState::Loading;
  if (st != g_state) { g_state = st; g_dirty = true; }

  if (!g_dirty && now - g_lastDraw < 1000) return;   // once a second for ages / countdowns
  g_dirty = false;
  g_lastDraw = now;

  layout(t);
  TFT_eSprite &s = *band;
  s.createPalette(const_cast<uint16_t *>(MAP_PALETTE.c), 16);
  for (int16_t y0 = 0; y0 < MAP_H; y0 += BAND_H) {
    drawBand(s, y0, t);
    drawStrip(s, y0, t);
    s.pushSprite(0, y0);
  }
}

MapAction mapTouch(const Traffic &t, int16_t x, int16_t y, char *hexOut) {
  g_dirty = true;
  if (x < 48 && y < 36) return MapAction::Back;
  if (x > 246 && y < 36) {
    settings().mapZoom = (zoom() + 1) % MAP_ZOOM_N;
    settingsSave();
    applyPollPlan();            // g_coveredNm < g_wantNm on zoom-out -> "Widening radar"
    return MapAction::None;
  }
  if (y >= STRIP_Y - 2) {       // the info strip opens the focus plane's card
    if (g_focus < 0 || g_state == MapState::OfflineRecent || g_state == MapState::OfflineCleared)
      return MapAction::None;
    strncpy(hexOut, t.ac[g_planes[g_focus].idx].hex, 7);
    return MapAction::OpenPlane;
  }
  if (g_state == MapState::OfflineRecent || g_state == MapState::OfflineCleared) return MapAction::None;
  const int hit = mapHitTest(t, ppn(), x, y, MAP_TAP_RADIUS_PX);
  if (hit < 0) {
    g_selected[0] = '\0';
  } else {
    strncpy(g_selected, t.ac[hit].hex, sizeof(g_selected));
  }
  appSetPinnedHex(g_selected);
  return MapAction::None;
}
