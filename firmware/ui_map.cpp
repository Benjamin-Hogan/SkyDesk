// Plane map screen - layout from docs/mockups/map_screen.py; rules in
// docs/08-plane-map.md + docs/09-map-v3.md. Rendered in 5 horizontal bands through one
// 320x48 4-bit sprite (08 -> "Rendering: band sprite"). NO anti-aliased calls here.
#include "aircraft_names.h"
#include "geo.h"
#include "map_model.h"
#include "route_client.h"
#include "settings.h"
#include "tracker.h"
#include "observer.h"
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
const Rect CHIPS[2] = {{0, 0, 48, 36}, {246, 0, MAP_W, 36}};   // back, zoom

// ---- per-frame layout, computed once and replayed into every band ---------
enum class Look : uint8_t { Normal, Focus, Inbound, Dim };
struct PlaneDraw {
  int8_t idx; float x, y; Look look; uint8_t members; bool tag, hidden, callout; int16_t tagX;
  Rect backing;   // x0 == x1: no backing
};
struct LabelDraw { const char *text; int16_t x, y; bool town; };

PlaneDraw g_planes[MAX_AIRCRAFT];
uint8_t   g_nPlanes = 0;
LabelDraw g_labels[48];
uint8_t   g_nLabels = 0;
Rect      g_taken[MAX_AIRCRAFT * 2 + 64];
uint8_t   g_nTaken = 0;
int8_t    g_focus = -1;               // index into g_planes (-1: off the map / none)
int       g_focusAc = -1;             // index into t.ac of the focus plane (-1: none)
char      g_focusHex[7] = "";         // the same plane by hex: touches resolve it against the
                                      // CURRENT traffic (the list re-sorts every poll)
bool      g_focusPassed = false;      // M7: the focus is the plane that just popped
WillPop   g_pop{};                    // focus will-pop (secs 0 = no)
bool      g_drawPop = false;          // pop square on the map (hidden within 16 px of the glyph)
Rect      g_popRect{0, 0, 0, 0};
float     g_fx = 0, g_fy = 0;         // focus projected (may be off the map)
float     g_leadX = 0, g_leadY = 0;   // +60 s point
bool      g_drawLeader = false;
bool      g_pointer = false;          // M4 edge pointer
float     g_ptX = 0, g_ptY = 0, g_ptUx = 0, g_ptUy = 0;
float     g_truncNm = 0;              // >0: only the nearest 40 are shown, ring at this distance
int8_t    g_nearestOut = -1;          // empty state: nearest aircraft beyond the view

enum class MapState : uint8_t { Live, Loading, OfflineRecent, OfflineCleared };
MapState  g_state = MapState::Live;
uint8_t   g_coveredNm = POLL_RADIUS_NM;   // radius the current snapshot covers
uint8_t   g_wantNm = POLL_RADIUS_NM;
char      g_selected[7] = "";
char      g_afterPop[7] = "";         // M7: the plane whose card just closed
uint32_t  g_afterPopMs = 0;
char      g_chipPassed[7] = "";       // 3.0 chip-passed focus (docs/11): opened from the weather chip
uint32_t  g_chipPassedEndMs = 0;
char      g_goneLabel[28] = "";       // it had already left: "<label>  out of range" for 4 s
uint32_t  g_goneMs = 0;
FocusHold g_hold{};
TapCycle  g_cycle{};
uint8_t   g_cycK = 0, g_cycN = 0;
uint32_t  g_cycleMs = 0;              // "k of N here" shows for 3 s after each tap
uint32_t  g_trafficVer = UINT32_MAX;
uint32_t  g_layoutVer = UINT32_MAX;   // traffic version of the last layout (focus hold polls)
uint32_t  g_lastDraw = 0;
bool      g_dirty = true;

uint8_t zoom() { return settings().mapZoom; }
float ppn() { return MAP_ZOOMS[zoom()].pxPerNm; }

bool isFree(const Rect &r, uint8_t upTo = 255) {
  if (r.x0 < 0 || r.x1 > MAP_W || r.y0 < 0) return false;
  for (uint8_t i = 0; i < g_nTaken && i < upTo; i++)
    if (overlaps(r, g_taken[i])) return false;
  return true;
}
void take(const Rect &r) {
  if (g_nTaken < sizeof(g_taken) / sizeof(g_taken[0])) g_taken[g_nTaken++] = r;
}
bool onMap(float x, float y) {   // the visible map area: not the strip, not under a chip
  if (x < 0 || x >= MAP_W || y < 0 || y >= STRIP_Y - 2) return false;
  for (const Rect &c : CHIPS)
    if (x >= c.x0 && x <= c.x1 && y >= c.y0 && y <= c.y1) return false;
  return true;
}

const char *callsignOf(const Aircraft &a) { return a.callsign[0] ? a.callsign : (a.reg[0] ? a.reg : a.hex); }

bool afterPopActive(uint32_t now) {
  return g_afterPop[0] && (int32_t)(now - g_afterPopMs) < (int32_t)(MAP_AFTER_POP_S * 1000UL);
}

bool chipPassedActive(uint32_t now) {
  return g_chipPassed[0] && (int32_t)(now - g_chipPassedEndMs) < 0;
}

bool goneActive(uint32_t now) { return g_goneMs && (int32_t)(now - g_goneMs) < 4000; }

// ONE width for tag placement and its backing (callsign, or altitude + the M5 tick).
int16_t tagWidth(const Aircraft &a) {
  char alt[10];
  mapAltTag(a.altFt, alt, sizeof(alt), false);
  setFont(*band, Font::Glcd);
  const int16_t wa = band->textWidth(alt) + (abs(a.vRateFpm) >= MIN_VRATE_FPM ? 7 : 0);
  return max<int16_t>(band->textWidth(callsignOf(a)), wa);
}

// What a backing must cover whole: the glyph, plus its cluster badge.
Rect glyphBox(const PlaneDraw &d) {
  if (d.members > 1) return {(int16_t)(d.x - 22), (int16_t)(d.y - 12), (int16_t)(d.x + 24), (int16_t)(d.y + 8)};
  return {(int16_t)(d.x - 8), (int16_t)(d.y - 8), (int16_t)(d.x + 8), (int16_t)(d.y + 8)};
}

void layout(const Traffic &t, uint32_t now) {
  const float k = ppn();
  const bool live = g_state == MapState::Live || g_state == MapState::Loading;
  g_nPlanes = g_nLabels = g_nTaken = 0;
  g_focus = -1;
  g_focusAc = -1;
  g_focusPassed = g_drawPop = g_drawLeader = g_pointer = false;
  g_pop = {0, 0, 0};
  g_nearestOut = -1;
  g_truncNm = (live && t.totalInRadius > t.n && t.n) ? t.ac[t.n - 1].distNm : 0;

  // chrome, OSM credit, strip, you (the first 6 rects are "chrome only")
  take(CHIPS[0]);
  take(CHIPS[1]);
  take({146, 0, 174, 30});
  take({0, STRIP_Y - 12, 60, MAP_H});
  take({0, STRIP_Y - 2, MAP_W, MAP_H});
  take({MAP_CX - 7, MAP_CY - 7, MAP_CX + 8, MAP_CY + 8});
  if (g_state == MapState::OfflineCleared) goto labels;

  // project (list is nearest-first; ground traffic never reaches the UI)
  for (uint8_t i = 0; i < t.n && g_nPlanes < MAX_AIRCRAFT; i++) {
    const Aircraft &a = t.ac[i];
    if (g_state == MapState::Loading && a.distNm > g_coveredNm) continue;
    float x, y;
    mapProject(a.lat, a.lon, k, x, y);
    if (x < -10 || x > MAP_W + 10 || y < -10 || y > STRIP_Y - 4) {
      if (g_nearestOut < 0) g_nearestOut = i;
      continue;
    }
    g_planes[g_nPlanes++] = {(int8_t)i, x, y, Look::Normal, 1, false, false, false, 0, {0, 0, 0, 0}};
  }

  {   // focus (09 M1/M7): selected > will-pop (held) > just popped > nearest on the map
    const bool newPoll = t.version != g_layoutVer;
    g_layoutVer = t.version;
    if (live) {
      if (g_chipPassed[0] && newPoll) {              // opened on a plane that has left: say so once
        bool present = false;
        for (uint8_t i = 0; i < t.n; i++) present |= strcmp(t.ac[i].hex, g_chipPassed) == 0;
        if (!present) {
          if (g_goneLabel[0] && !g_goneMs) g_goneMs = now ? now : 1;
          g_chipPassed[0] = '\0';
        }
      }
      g_focusAc = mapPickFocus(t, g_selected, afterPopActive(now) ? g_afterPop : "", g_hold, newPoll,
                               chipPassedActive(now) ? g_chipPassed : "");
    }
    else
      for (uint8_t i = 0; i < t.n && g_focusAc < 0; i++)
        if (g_selected[0] && strcmp(t.ac[i].hex, g_selected) == 0) g_focusAc = i;
    if (g_focusAc < 0 && g_nPlanes) g_focusAc = g_planes[0].idx;
    g_focusHex[0] = 0;
    if (g_focusAc >= 0) {
      const Aircraft &f = t.ac[g_focusAc];
      snprintf(g_focusHex, sizeof(g_focusHex), "%s", f.hex);
      mapProject(f.lat, f.lon, k, g_fx, g_fy);
      for (uint8_t p = 0; p < g_nPlanes; p++)
        if (g_planes[p].idx == g_focusAc && onMap(g_fx, g_fy)) g_focus = p;
      mapWillPop(f, g_pop);
      g_focusPassed = !g_pop.secs && ((afterPopActive(now) && strcmp(f.hex, g_afterPop) == 0) ||
                                      (chipPassedActive(now) && strcmp(f.hex, g_chipPassed) == 0));
    }
  }

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

  if (live && g_focusAc >= 0 && t.ac[g_focusAc].seenPos <= MAX_SEEN_POS_S) {
    const Aircraft &f = t.ac[g_focusAc];
    // M2: pop square (never covered; hidden within 16 px of the glyph, where the
    // countdown already says it) and the 60 s leader - off the map only when it leads
    // to a pop square; never on a plane that has already passed (M7).
    if (g_pop.secs) {
      float qx, qy;
      mapProject(g_pop.lat, g_pop.lon, k, qx, qy);
      if (onMap(qx, qy) && hypotf(qx - g_fx, qy - g_fy) >= 16) {
        g_drawPop = true;
        g_popRect = {(int16_t)(qx - 5), (int16_t)(qy - 5), (int16_t)(qx + 5), (int16_t)(qy + 5)};
        take(g_popRect);
      }
    }
    if (!g_focusPassed && (g_focus >= 0 || g_drawPop)) {
      double la, lo;
      mapAhead(f, MAP_LEADER_S, la, lo);
      mapProject(la, lo, k, g_leadX, g_leadY);
      g_drawLeader = true;
    }
    // M4: focus off the map -> 14 px pointer on the inset edge, sliding clear of chrome
    if (g_focus < 0) {
      const float dx = g_fx - MAP_CX, dy = g_fy - MAP_CY;
      const float ln = hypotf(dx, dy);
      if (ln > 1) {
        const float kx = dx ? fabsf((dx > 0 ? 154.0f : -154.0f) / dx) : 1e9f;
        const float ky = dy ? fabsf(((dy > 0 ? STRIP_Y - 8.0f : 6.0f) - MAP_CY) / dy) : 1e9f;
        const float kk = min(kx, ky);
        float ex = MAP_CX + dx * kk, ey = MAP_CY + dy * kk;
        const bool onTop = fabsf(ey - 6) < 1;
        const int16_t zones[3][3] = {{0, 50, 38}, {140, 180, 32}, {242, 320, 38}};
        for (const auto &z : zones) {
          if (ex >= z[0] && ex <= z[1] && ey <= z[2]) {
            if (onTop && z[0] > 0 && z[1] < 320) ex = ex < (z[0] + z[1]) / 2 ? z[0] - 4 : z[1] + 4;
            else if (onTop) ex = z[0] == 0 ? z[1] + 4 : z[0] - 4;
            else ey = z[2] + 20;                     // never reads as a second back button
          }
        }
        g_pointer = true;
        g_ptX = ex; g_ptY = ey; g_ptUx = dx / ln; g_ptUy = dy / ln;
      }
    }
  }

  {   // airports before tags
    setFont(*band, Font::Glcd);
    for (uint8_t i = 0; obsInGate() && i < MAP_AIRPORT_N && g_nLabels < 48; i++) {   // docs/12 gate
      float x, y;
      mapProject(MAP_AIRPORTS[i].lat, MAP_AIRPORTS[i].lon, k, x, y);
      const int16_t w = band->textWidth(MAP_AIRPORTS[i].name);
      const Rect r{(int16_t)(x + 5), (int16_t)(y - 4), (int16_t)(x + 6 + w), (int16_t)(y + 5)};
      if (isFree(r)) {
        take(r);
        g_labels[g_nLabels++] = {MAP_AIRPORTS[i].name, (int16_t)(x + 5), (int16_t)(y + 4), false};
      }
    }
  }

  if (live) {   // tags: focus first, then by distance; right, else left
    for (int pass = 0; pass < 2; pass++) {
      for (uint8_t p = 0; p < g_nPlanes; p++) {
        PlaneDraw &d = g_planes[p];
        if ((pass == 0) != (p == g_focus) || d.members > 1 || d.look == Look::Dim || d.hidden) continue;
        const int16_t w = tagWidth(t.ac[d.idx]);
        const int16_t gap = p == g_focus ? 14 : 10;   // clear of the focus ring
        for (int16_t x0 : {(int16_t)(d.x + gap), (int16_t)(d.x - gap - w)}) {
          const Rect r{(int16_t)(x0 - 1), (int16_t)(d.y - 9), (int16_t)(x0 + w + 1), (int16_t)(d.y + 9)};
          if (isFree(r)) { take(r); d.tag = true; d.tagX = x0; break; }
        }
        if (d.tag || p != g_focus) continue;
        // The focus tag always lands (09 M8, v3-R2-4): a TAG-SIZED backing; every glyph
        // or badge it touches is not drawn (no fragments) and counts as hidden. The side
        // hiding the fewest wins; if both touch the focus ring, step outward (callout).
        const Rect ring{(int16_t)(d.x - 12), (int16_t)(d.y - 12), (int16_t)(d.x + 12), (int16_t)(d.y + 12)};
        int best = 1000;
        Rect bestR{0, 0, 0, 0};
        int16_t bestX = 0, bestOut = gap;
        for (int16_t out : {gap, (int16_t)22, (int16_t)30, (int16_t)38, (int16_t)46}) {
          for (int16_t x0 : {(int16_t)(d.x + out), (int16_t)(d.x - out - w)}) {
            const Rect r{(int16_t)(x0 - 2), (int16_t)(d.y - 10), (int16_t)(x0 + w + 2), (int16_t)(d.y + 10)};
            if (!isFree(r, 6) || overlaps(r, ring) || (g_drawPop && overlaps(r, g_popRect))) continue;
            int cover = 0;
            for (uint8_t q = 0; q < g_nPlanes; q++)
              if (q != p && overlaps(r, glyphBox(g_planes[q]))) cover += g_planes[q].members;
            if (cover < best) { best = cover; bestR = r; bestX = x0; bestOut = out; }
          }
          if (best < 1000) break;
        }
        if (best < 1000) {
          d.tag = true;
          d.tagX = bestX;
          d.backing = bestR;
          d.callout = bestOut > gap;
          take(bestR);
          for (uint8_t q = 0; q < g_nPlanes; q++)
            if (q != p && overlaps(bestR, glyphBox(g_planes[q]))) g_planes[q].hidden = true;
        }
      }
    }
  }

labels:
  // towns only where free; your own town is skipped
  setFont(*band, Font::F2);
  for (uint8_t i = 0; obsInGate() && i < MAP_TOWN_N && g_nLabels < 48; i++) {
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

void drawBand(TFT_eSprite &s, int16_t y0, const Traffic &t) {
  const float k = ppn();
  const int16_t cy = MAP_CY - y0;   // everything is drawn shifted by -y0; the sprite clips
  const bool live = g_state == MapState::Live || g_state == MapState::Loading;

  if (obsInGate())
    memcpy(s.getPointer(), MAP_ZOOMS[zoom()].img + (size_t)y0 * (MAP_W / 2), BAND_H * (MAP_W / 2));
  else                                                    // off the gate: no streets (docs/12)
    memset(s.getPointer(), 0, BAND_H * (MAP_W / 2));      // index 0 = M_BG in both nibbles
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
      if (g_planes[p].hidden) continue;
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

  // M2: the focus leader (an amber dot every 3 px, from 12 px out) and the pop square
  if (g_drawLeader) {
    const float len = hypotf(g_leadX - g_fx, g_leadY - g_fy);
    const int n = max(1, (int)(len / 3));
    for (int i = 3; i <= n; i++) {
      const float px = g_fx + (g_leadX - g_fx) * i / n, py = g_fy + (g_leadY - g_fy) * i / n;
      if (hypotf(px - g_fx, py - g_fy) > 12 && onMap(px, py)) s.drawPixel(lroundf(px), lroundf(py) - y0, M_PLANE);
    }
  }
  if (g_drawPop) {
    float qx, qy;
    mapProject(g_pop.lat, g_pop.lon, k, qx, qy);
    s.drawRect(lroundf(qx) - 3, lroundf(qy) - 3 - y0, 7, 7, M_PLANE);
  }

  const float scale = zoom() == 2 ? 0.45f : 0.6f;
  for (uint8_t p = 0; p < g_nPlanes; p++) {
    const PlaneDraw &d = g_planes[p];
    if (d.hidden) continue;                              // under the focus tag's backing
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
  }
  for (uint8_t p = 0; p < g_nPlanes; p++) {   // tags after every glyph: backings sit on top
    const PlaneDraw &d = g_planes[p];
    if (!d.tag) continue;
    const Aircraft &a = t.ac[d.idx];
    if (d.callout) {                           // 1 px amber line, ring -> backing
      const bool right = d.backing.x0 > d.x;
      const int16_t xr = lroundf(d.x) + (right ? 11 : -11), xt = right ? d.backing.x0 : d.backing.x1;
      s.drawFastHLine(min(xr, xt), lroundf(d.y) - y0, abs(xt - xr), M_PLANE);
    }
    if (d.backing.x1 > d.backing.x0)
      s.fillRect(d.backing.x0, d.backing.y0 - y0, d.backing.x1 - d.backing.x0, d.backing.y1 - d.backing.y0, M_PANEL);
    char alt[10];
    mapAltTag(a.altFt, alt, sizeof(alt), false);
    drawText(s, callsignOf(a), d.tagX, lroundf(d.y) - 1 - y0, Font::Glcd, d.look == Look::Focus ? M_PLANE : M_TEXT);
    const int16_t aw = drawText(s, alt, d.tagX, lroundf(d.y) + 8 - y0, Font::Glcd, M_MUTED);
    if (abs(a.vRateFpm) >= MIN_VRATE_FPM) {    // M5: climbing / descending tick
      const int16_t tx = d.tagX + aw + 2, ty = lroundf(d.y) + 4 - y0;
      if (a.vRateFpm > 0) s.fillTriangle(tx, ty + 2, tx + 4, ty + 2, tx + 2, ty - 2, M_MUTED);
      else s.fillTriangle(tx, ty - 2, tx + 4, ty - 2, tx + 2, ty + 2, M_MUTED);
    }
  }

  if (g_pointer) {   // M4: 14 px amber triangle with a 1 px BG outline, pointing at the focus
    for (int pass = 0; pass < 2; pass++) {
      const float L = pass ? 14 : 17, half = pass ? 7 : 8.5f, off = pass ? 0 : 1;
      const float tx = g_ptX + g_ptUx * off, ty = g_ptY + g_ptUy * off;
      const float bx = tx - g_ptUx * L, by = ty - g_ptUy * L;
      s.fillTriangle(lroundf(tx), lroundf(ty) - y0, lroundf(bx - g_ptUy * half), lroundf(by + g_ptUx * half) - y0,
                     lroundf(bx + g_ptUy * half), lroundf(by - g_ptUx * half) - y0, pass ? M_PLANE : M_BG);
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
  if (obsInGate()) {
    drawText(s, "(c) OSM", 4, STRIP_Y - 4 - y0, Font::Glcd, M_DIM);
  } else {
    char note[48];
    snprintf(note, sizeof(note), "No streets here - built for %s", OBS_PLACE);
    drawText(s, note, 4, STRIP_Y - 4 - y0, Font::Glcd, M_DIM);
  }
}

int16_t f2Width(const char *s) {
  setFont(*band, Font::F2);
  return band->textWidth(s);
}

void drawStrip(TFT_eSprite &s, int16_t y0, const Traffic &t, uint32_t now) {
  if (y0 + BAND_H <= STRIP_Y) return;
  const int16_t oy = -y0, base = 231 + oy;
  s.fillRect(0, STRIP_Y + oy, MAP_W, MAP_H - STRIP_Y, M_PANEL);
  char buf[40];
  switch (g_state) {
    case MapState::OfflineRecent: {
      drawText(s, "Traffic offline", 10, base, Font::F2, M_WARN);   // "Radar" = rain (v3-R1-5)
      snprintf(buf, sizeof(buf), "positions %lu s old", (unsigned long)((now - t.fetchedMs) / 1000));
      drawText(s, buf, 310, base, Font::F2, M_MUTED, R_BASELINE);
      return;
    }
    case MapState::OfflineCleared:
      drawText(s, "No live traffic", 10, base, Font::F2, M_WARN);
      snprintf(buf, sizeof(buf), "retrying in %d s", (int)max<int32_t>(0, (int32_t)(t.nextRetryMs - now) / 1000));
      drawText(s, buf, 310, base, Font::F2, M_MUTED, R_BASELINE);
      return;
    case MapState::Loading:   // the strip keeps describing the focus plane
    case MapState::Live:
      break;
  }
  if (goneActive(now)) {      // 3.0: the chip's passed plane has left the traffic (round 3 S1)
    drawText(s, g_goneLabel, 10, base, Font::F2, M_MUTED);
    drawText(s, "out of range", 310, base, Font::F2, M_MUTED, R_BASELINE);
    return;
  }
  if (g_focusAc < 0) {
    snprintf(buf, sizeof(buf), "Nothing within %d mi", ZOOM_MI[zoom()]);
    drawText(s, buf, 10, base, Font::F2, M_MUTED);
    if (g_nearestOut >= 0) {
      const Aircraft &n = t.ac[g_nearestOut];
      snprintf(buf, sizeof(buf), "nearest %.1f mi %s", n.distNm * 1.15078f, geo::compass8(n.azDeg));
      drawText(s, buf, 310, base, Font::F2, M_MUTED, R_BASELINE);
    }
    return;
  }
  const Aircraft &a = t.ac[g_focusAc];
  RouteInfo r;
  const bool haveRoute = routeGet(a.hex, r);
  PlaneLabels L;
  planeLabels(a, haveRoute ? &r : nullptr, L);
  drawPlaneGlyph(s, 14, 227 + oy, a.track >= 0 ? a.track : 0, 0.5f, M_PLANE);

  // right side (09 table): the state and fallback choice is the host-tested mapStripRight()
  const char *type = L.type[0] ? L.type : "Unknown type";
  const char *opName = L.airline ? L.op : callsignOf(a);
  StripKind kind = StripKind::Default;
  if (g_cycN > 1 && (int32_t)(now - g_cycleMs) < 3000) kind = StripKind::Cycle;
  else if (g_pop.secs) kind = StripKind::WillPop;
  else if (g_focusPassed) kind = StripKind::Passed;
  else if (a.distNm <= ENTER_RADIUS_NM) kind = StripKind::InDisc;
  StripSeg seg[2];
  bool preferred = false;
  const uint8_t ns = mapStripRight(kind, a, g_cycK, g_cycN, g_pop.secs, f2Width(type), f2Width(opName), f2Width,
                                   seg, &preferred);
  static_assert(sizeof(seg[0].text) >= sizeof("overhead in ~60 s"), "strip text buffer too small (v3-R1-13)");
  int16_t xr = 300;
  for (int i = ns - 1; i >= 0; i--) {
    if (i != ns - 1) {
      drawSep(s, xr - 5, 226 + oy, M_DIM);
      xr -= 10;
    }
    xr -= drawText(s, seg[i].text, xr, base, Font::F2, seg[i].pal, R_BASELINE);
  }
  const int16_t avail = xr - 8 - 26;

  // left: "<operator> . <type>" - type before flight number (v1 priority). The flight
  // number only if the PREFERRED right side was kept and it fits; if still too wide,
  // truncate the OPERATOR, never the type.
  char op[32];
  snprintf(op, sizeof(op), "%s", opName);
  const int16_t tw = f2Width(type);
  if (L.airline && preferred) {
    const char *num = a.callsign;
    while (*num && !isdigit((unsigned char)*num)) num++;
    char withNum[32];
    snprintf(withNum, sizeof(withNum), "%s %s", L.op, num);
    if (*num && f2Width(withNum) + 10 + tw <= avail) snprintf(op, sizeof(op), "%s", withNum);
  }
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
  g_afterPop[0] = '\0';
  g_chipPassed[0] = '\0';
  g_goneLabel[0] = '\0';
  g_goneMs = 0;
  g_hold = FocusHold{};
  g_cycle = TapCycle{};
  g_cycN = 0;
  appSetPinnedHex("");
}

void mapOpenPassed(const char *hex, const char *label, uint32_t untilMs) {
  snprintf(g_chipPassed, sizeof(g_chipPassed), "%s", hex ? hex : "");
  snprintf(g_goneLabel, sizeof(g_goneLabel), "%s", label ? label : "");
  g_chipPassedEndMs = untilMs;
  g_goneMs = 0;
  g_dirty = true;
}

void mapCardClosed(const char *hex) {
  snprintf(g_afterPop, sizeof(g_afterPop), "%s", hex ? hex : "");
  g_afterPopMs = millis();
  g_dirty = true;
}

bool mapFocusWillPop() { return g_pop.secs > 0; }

void mapUpdate(const Traffic &t, bool trafficUp, uint32_t now) {
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
  const uint32_t age = (int32_t)(now - t.fetchedMs) > 0 ? now - t.fetchedMs : 0;
  MapState st = MapState::Live;
  if (!trafficUp) st = age <= MAP_OFFLINE_KEEP_S * 1000UL ? MapState::OfflineRecent : MapState::OfflineCleared;
  else if (g_coveredNm < g_wantNm) st = MapState::Loading;
  if (st != g_state) { g_state = st; g_dirty = true; }

  if (!g_dirty && now - g_lastDraw < 1000) return;   // once a second for ages / countdowns
  g_dirty = false;
  g_lastDraw = now;

  layout(t, now);
  TFT_eSprite &s = *band;
  s.createPalette(const_cast<uint16_t *>(MAP_PALETTE.c), 16);
  for (int16_t y0 = 0; y0 < MAP_H; y0 += BAND_H) {
    drawBand(s, y0, t);
    drawStrip(s, y0, t, now);
    s.pushSprite(0, y0);
  }
}

MapAction mapTouch(const Traffic &t, int16_t x, int16_t y, char *hexOut) {
  g_dirty = true;
  if (x < 48 && y < 36) return MapAction::Back;
  if (x > 246 && y < 36) {
    settings().mapZoom = (zoom() + 1) % MAP_ZOOM_N;
    settingsSave();
    applyPollPlan();            // g_coveredNm < g_wantNm on zoom-out -> "Widening"
    g_cycle = TapCycle{};
    return MapAction::None;
  }
  if (y >= STRIP_Y - 2) {       // the info strip opens the focus plane's card
    if (!g_focusHex[0] || g_state == MapState::OfflineRecent || g_state == MapState::OfflineCleared)
      return MapAction::None;
    snprintf(hexOut, 7, "%s", g_focusHex);
    return MapAction::OpenPlane;
  }
  if (g_state == MapState::OfflineRecent || g_state == MapState::OfflineCleared) return MapAction::None;
  // M3: select; a tap near the frozen list's anchor cycles through the planes there
  const uint32_t now = millis();
  uint8_t k, n;
  const int sel = mapTap(t, ppn(), x, y, now, g_focusHex, g_cycle, k, n);
  if (sel == -2) return MapAction::None;             // panel bounce
  if (sel < 0) {
    g_selected[0] = '\0';
    g_cycN = 0;
  } else {
    snprintf(g_selected, sizeof(g_selected), "%s", t.ac[sel].hex);
    g_cycK = k;
    g_cycN = n;
    g_cycleMs = now;
  }
  appSetPinnedHex(g_selected);
  return MapAction::None;
}
