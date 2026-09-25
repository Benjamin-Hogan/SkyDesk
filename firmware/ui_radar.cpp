// Rain radar screen - layout from docs/mockups/radar_screen.py; rules in
// docs/10-rain-radar.md. The same band pipeline as the plane map: the radar basemap
// (zoom R) is memcpy'd from flash into the 320x48 4-bit band sprite, then the frame's
// 4-bit rain layer (streamed from SD, /radar/<valid>.bin) overwrites non-zero nibbles.
// NO anti-aliased calls here.
#include "basemap.h"
#include "geo.h"
#include "radar_client.h"
#include "sd_store.h"
#include "ui_internal.h"

#include <math.h>

using namespace ui;

namespace {

constexpr int16_t BAND_H = 48;
constexpr int16_t STRIP_Y = 214;
constexpr uint8_t RAIN_SLOT[6] = {0, M_PRIMARY, M_RUNWAY, M_TRAIL, M_PLANE, M_PLANE_DIM};   // level -> slot
constexpr uint8_t P_RAINTEXT = M_ZONE;

// RADAR_PALETTE (docs/10 -> Palette): the map palette, darker roads/water, rain levels in
// the plane/runway/trail/primary slots (no amber anywhere), a dimmed ramp when stale.
uint16_t g_pal[16];
void buildPalette(bool stale) {
  memcpy(g_pal, MAP_PALETTE.c, sizeof(g_pal));
  g_pal[M_TRUNK] = RGB565(0x1E, 0x28, 0x3A);
  g_pal[M_MOTORWAY] = RGB565(0x2A, 0x34, 0x48);
  g_pal[M_WATER] = RGB565(0x11, 0x24, 0x3A);
  static const uint16_t LIVE[6] = {0, RGB565(0x23, 0x58, 0xA8), RGB565(0x3C, 0x8E, 0xF0), RGB565(0x8A, 0xD8, 0xFF),
                                   RGB565(0xF4, 0xF7, 0xFF), RGB565(0xFF, 0x4F, 0xD2)};
  static const uint16_t STALE[6] = {0, RGB565(0x1A, 0x3A, 0x66), RGB565(0x2A, 0x5A, 0x94), RGB565(0x4E, 0x7A, 0x92),
                                    RGB565(0x7E, 0x86, 0x98), RGB565(0x8A, 0x4A, 0x80)};
  for (uint8_t k = 1; k <= 5; k++) g_pal[RAIN_SLOT[k]] = stale ? STALE[k] : LIVE[k];
  g_pal[P_RAINTEXT] = COL_RAIN;
}

enum class RState : uint8_t { Live, Updating, Stale, Unreadable, Partial, Offline, NoSd, First };

RadarStatus g_st{};
uint32_t g_stVer = UINT32_MAX;
uint8_t  g_wet[RADAR_WET_W * RADAR_WET_H / 8];   // union over the loop (label occlusion)
uint8_t  g_fi = 0;                               // frame on screen (index into g_st.valid)
uint32_t g_frameMs = 0;
bool     g_dirty = true;
RState   g_state = RState::Offline;
bool     g_mounted = false;           // the card is usable (sdAcquire)
bool     g_drawn = false;             // something is on screen since radarEnter()

struct Label { const char *name; int16_t x, y, lx, ly; uint8_t datum; };
Label   g_labels[10];
uint8_t g_nLabels = 0;

uint32_t epochNow() { return (uint32_t)time(nullptr); }

bool looping() {
  return g_st.n >= 2 && g_st.echo && g_state != RState::Stale && g_state != RState::Offline;
}

RState computeState() {
  if (!g_st.sdOk || !g_mounted) return RState::NoSd;
  const uint32_t now = epochNow();
  if (!g_st.n) return g_st.lastFailed ? RState::Offline : RState::First;   // first download in flight
  if (now < 1700000000u) return RState::Live;
  const int32_t age = (int32_t)(now - g_st.valid[g_st.n - 1]);   // signed: clock skew is not "old"
  if (age >= (int32_t)(RADAR_CLEAR_MIN * 60)) return RState::Offline;
  if (g_st.unreadable) return RState::Unreadable;
  if (age >= (int32_t)(RADAR_STALE_MIN * 60)) return g_st.lastFailed ? RState::Stale : RState::Updating;
  if (g_st.quorumPct < RADAR_QUORUM_MIN) return RState::Partial;
  return RState::Live;
}

bool wetAt(int16_t x, int16_t y) {
  if (x < 0 || y < 0 || x >= RADAR_W || y >= RADAR_H) return false;
  const uint16_t cell = (y / 4) * RADAR_WET_W + x / 4;
  return (g_wet[cell >> 3] >> (cell & 7)) & 1;
}
bool dryRect(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  for (int16_t y = y0 - (y0 % 4); y <= y1; y += 4)
    for (int16_t x = x0 - (x0 % 4); x <= x1; x += 4)
      if (wetAt(x, y)) return false;
  return true;
}

// Towns (docs/10 -> Towns): one per compass octant beyond 15 mi first, then by population
// (RADAR_TOWNS is population order); max 10; never over rain in ANY frame of the loop.
void placeLabels() {
  struct R { int16_t x0, y0, x1, y1; };
  R taken[40] = {{0, 0, 48, 36}, {200, 0, 320, 36}, {146, 0, 174, 30}, {0, STRIP_Y - 14, 110, 240},
                 {RADAR_CX - 8, RADAR_CY - 8, RADAR_CX + 8, RADAR_CY + 8},
                 {RADAR_CX + 70, RADAR_CY + 64, RADAR_CX + 106, RADAR_CY + 78}};
  uint8_t nt = 6;
  bool done[64] = {false};
  g_nLabels = 0;
  setFont(*band, Font::Glcd);
  auto freeR = [&](const R &r) {
    if (r.x0 < 2 || r.x1 > 318 || r.y0 < 0 || r.y1 >= STRIP_Y - 2) return false;
    for (uint8_t i = 0; i < nt; i++)
      if (!(r.x1 <= taken[i].x0 || r.x0 >= taken[i].x1 || r.y1 <= taken[i].y0 || r.y0 >= taken[i].y1)) return false;
    return true;
  };
  auto tryPlace = [&](uint8_t ti) {
    const MapLabel &m = RADAR_TOWNS[ti];
    float fx, fy;
    const double coslat = cos(OBS_LAT * DEG_TO_RAD);
    fx = RADAR_CX + (m.lon - OBS_LON) * 60 * coslat * RADAR_PPN;
    fy = RADAR_CY - (m.lat - OBS_LAT) * 60 * RADAR_PPN;
    const int16_t x = lroundf(fx), y = lroundf(fy), w = band->textWidth(m.name);
    const struct { int16_t lx, ly; uint8_t d; } opts[4] = {
        {x, (int16_t)(y - 3), C_BASELINE}, {(int16_t)(x + 4), (int16_t)(y + 4), L_BASELINE},
        {(int16_t)(x - 4), (int16_t)(y + 4), R_BASELINE}, {x, (int16_t)(y + 11), C_BASELINE}};
    for (const auto &o : opts) {
      const int16_t x0 = o.d == C_BASELINE ? o.lx - w / 2 : o.d == L_BASELINE ? o.lx : o.lx - w;
      const R r{(int16_t)(x0 - 1), (int16_t)(o.ly - 8), (int16_t)(x0 + w + 1), (int16_t)(o.ly + 1)};
      if (freeR(r) && dryRect(r.x0, r.y0, r.x1, r.y1) && dryRect(x - 2, y - 2, x + 2, y + 2) && nt < 38) {
        taken[nt++] = r;
        taken[nt++] = {(int16_t)(x - 2), (int16_t)(y - 2), (int16_t)(x + 2), (int16_t)(y + 2)};
        g_labels[g_nLabels++] = {m.name, x, y, o.lx, o.ly, o.d};
        return true;
      }
    }
    return false;
  };
  for (uint8_t oct = 0; oct < 8 && g_nLabels < 10; oct++) {     // spread first
    for (uint8_t i = 0; i < RADAR_TOWN_N && i < 64; i++) {       // biggest first within the octant
      const MapLabel &m = RADAR_TOWNS[i];
      if (strcmp(m.name, "Gilbert") == 0) continue;               // your own town is the dot
      double d, az;
      geo::distBearing({OBS_LAT, OBS_LON}, {m.lat, m.lon}, d, az);
      if (d / geo::M_PER_NM * 1.15078 < 15 || (int)(fmod(az + 22.5, 360) / 45) != oct) continue;
      if (tryPlace(i)) { done[i] = true; break; }
    }
  }
  for (uint8_t i = 0; i < RADAR_TOWN_N && i < 64 && g_nLabels < 10; i++) {   // then population
    if (done[i] || strcmp(RADAR_TOWNS[i].name, "Gilbert") == 0) continue;
    if (tryPlace(i)) done[i] = true;
  }
}

// New frame list: OR the wet masks of the loop (or just the newest), place labels,
// then acknowledge it - the net task may now delete frames that left the list.
bool takeSnapshot() {
  if (!sdLock(0)) return false;                  // the net task is converting: retry next tick
  appGetRadar(g_st);
  g_stVer = g_st.version;
  g_state = computeState();
  memset(g_wet, 0, sizeof(g_wet));
  const uint8_t first = looping() ? 0 : (g_st.n ? g_st.n - 1 : 0);
  for (uint8_t i = first; i < g_st.n; i++) {
    char p[40];
    radarFramePath(g_st.valid[i], p, sizeof(p));
    File32 f = sdFs().open(p, O_RDONLY);
    RadarFrameHdr h;
    if (f && f.read((uint8_t *)&h, sizeof(h)) == sizeof(h) && h.magic == RADAR_MAGIC)
      for (size_t k = 0; k < sizeof(g_wet); k++) g_wet[k] |= h.wet[k];
    if (f) f.close();
  }
  sdUnlock();
  placeLabels();
  if (g_fi >= g_st.n) g_fi = g_st.n ? g_st.n - 1 : 0;
  appRadarAck(g_stVer);
  g_dirty = true;
  return true;
}

void haloText(TFT_eSprite &s, const char *t, int16_t x, int16_t y, uint8_t c, uint8_t datum = L_BASELINE) {
  for (int dx = -1; dx <= 1; dx++)
    for (int dy = -1; dy <= 1; dy++)
      if (dx || dy) drawText(s, t, x + dx, y + dy, Font::Glcd, M_BG, datum);
  drawText(s, t, x, y, Font::Glcd, c, datum);
}

void chevronLeft(TFT_eSprite &s, int16_t x, int16_t y, uint8_t c) {
  s.fillTriangle(x + 7, y - 7, x + 7, y - 4, x, y, c);
  s.fillTriangle(x + 7, y - 4, x + 3, y, x, y, c);
  s.fillTriangle(x + 7, y + 7, x + 7, y + 4, x, y, c);
  s.fillTriangle(x + 7, y + 4, x + 3, y, x, y, c);
}

void dottedRing(TFT_eSprite &s, int16_t cy, float r, uint8_t c, float step) {
  const int n = (int)(2 * PI * r / step);
  for (int i = 0; i < n; i++) {
    const float a = 2 * PI * i / n;
    const int16_t x = RADAR_CX + lroundf(r * cosf(a)), y = cy + lroundf(r * sinf(a));
    if (y + (RADAR_CY - cy) < STRIP_Y) s.drawPixel(x, y, c);
  }
}

void fmtTime(uint32_t valid, char *out, size_t n) {
  const time_t t = valid;
  struct tm lt;
  localtime_r(&t, &lt);
  char hm[8], ap[4];
  fmtClock(lt, hm, sizeof(hm), ap, sizeof(ap));
  snprintf(out, n, "%s %s", hm, ap);
}

void drawBand(TFT_eSprite &s, int16_t y0, File32 *frame) {
  const int16_t cy = RADAR_CY - y0;
  uint8_t *buf = (uint8_t *)s.getPointer();
  memcpy(buf, RADAR_BASEMAP.img + (size_t)y0 * (RADAR_W / 2), BAND_H * (RADAR_W / 2));
  if (frame) {                                  // rain: overwrite non-zero nibbles, visible rows only
    uint8_t row[RADAR_W / 2];
    for (int16_t r = 0; r < BAND_H && y0 + r < STRIP_Y; r++) {
      if (!frame->seek(sizeof(RadarFrameHdr) + (uint32_t)(y0 + r) * (RADAR_W / 2)) ||
          frame->read(row, sizeof(row)) != sizeof(row))
        break;
      uint8_t *line = buf + r * (RADAR_W / 2);
      for (int16_t i = 0; i < RADAR_W / 2; i++) {
        const uint8_t v = row[i];
        if (!v) continue;
        const uint8_t hi = v >> 4, lo = v & 0x0F;
        if (hi) line[i] = (uint8_t)((RAIN_SLOT[hi > 5 ? 5 : hi] << 4) | (line[i] & 0x0F));
        if (lo) line[i] = (uint8_t)((line[i] & 0xF0) | RAIN_SLOT[lo > 5 ? 5 : lo]);
      }
    }
  }
  dottedRing(s, cy, 100, M_DIM, 7);
  haloText(s, "50 mi", RADAR_CX + 74, RADAR_CY + 74 - y0, M_DIM);
  for (uint8_t i = 0; i < g_nLabels; i++) {
    const Label &l = g_labels[i];
    s.fillRect(l.x - 1, l.y - 1 - y0, 2, 2, M_MUTED);
    haloText(s, l.name, l.lx, l.ly - y0, M_MUTED, l.datum);
  }
  s.fillCircle(RADAR_CX, cy, 7, M_BG);          // you: survives pale-cyan / white rain
  s.fillCircle(RADAR_CX, cy, 5, M_TEXT);
  s.fillCircle(RADAR_CX, cy, 3, M_YOU);

  s.fillRoundRect(4, 4 - y0, 36, 24, 12, M_PANEL);
  chevronLeft(s, 17, 16 - y0, M_TEXT);
  s.fillTriangle(160, 3 - y0, 155, 12 - y0, 165, 12 - y0, M_MUTED);
  drawText(s, "N", 160, 24 - y0, Font::Glcd, M_MUTED, C_BASELINE);
  if (g_st.n && g_state != RState::Offline && g_state != RState::NoSd) {   // every frame carries its time
    char lab[12];
    const uint8_t shown = looping() ? g_fi : g_st.n - 1;
    fmtTime(g_st.valid[shown], lab, sizeof(lab));
    const bool warn = g_state == RState::Stale || g_state == RState::Unreadable;   // (First: no frame yet)
    const uint8_t c = warn ? M_WARN : (shown == g_st.n - 1 && g_state != RState::Updating) ? M_TEXT : M_MUTED;
    setFont(s, Font::F2);
    const int16_t zw = s.textWidth(lab) + 18;
    s.fillRoundRect(316 - zw, 4 - y0, zw, 24, 12, M_PANEL);
    drawText(s, lab, 316 - zw / 2, 21 - y0, Font::F2, c, C_BASELINE);
  }
  haloText(s, "(c) OSM  IEM", 4, STRIP_Y - 4 - y0, M_DIM);
}

void drawStrip(TFT_eSprite &s, int16_t y0) {
  if (y0 + BAND_H <= STRIP_Y) return;
  const int16_t oy = -y0, base = 231 + oy;
  s.fillRect(0, STRIP_Y + oy, RADAR_W, RADAR_H - STRIP_Y, M_PANEL);
  char buf[48];
  if (g_state == RState::NoSd) {
    // say WHY (there may be no serial link): "no card or not FAT32", "write failed", ...
    char why[48];
    snprintf(why, sizeof(why), "SD card: %s", sdStatus());
    drawText(s, why, 10, base, Font::F2, M_WARN);
    return;
  }
  if (g_state == RState::First) {
    drawText(s, "Loading radar...", 10, base, Font::F2, M_MUTED);
    return;
  }
  if (g_state == RState::Offline) {   // say WHY (there may be no serial link)
    drawText(s, "Radar offline", 10, base, Font::F2, M_WARN);
    drawText(s, g_st.err[0] ? g_st.err : "retrying", 310, base, Font::F2, M_MUTED, R_BASELINE);
    return;
  }
  int16_t x = 10;
  if (looping() || g_state == RState::Stale) {
    // frame bar: filled DIM = downloaded, TEXT = on screen, hollow = not downloaded yet
    uint32_t set[RADAR_FRAMES];
    const uint8_t ns = radarLoopSet(g_st.newestValid ? g_st.newestValid : g_st.valid[g_st.n - 1], set, RADAR_FRAMES,
                                    RADAR_FRAME_STEP_S);
    for (uint8_t i = 0; i < ns; i++) {
      const int16_t sx = x + (RADAR_FRAMES - ns + i) * 8;
      const uint32_t want = set[i];
      int8_t idx = -1;
      for (uint8_t k = 0; k < g_st.n; k++)
        if (g_st.valid[k] == want) idx = k;
      if (g_state == RState::Stale) s.fillRect(sx, 222 + oy, 6, 10, M_DIM);
      else if (idx < 0) s.drawRect(sx, 222 + oy, 6, 10, M_DIM);
      else s.fillRect(sx, 222 + oy, 6, 10, idx == g_fi ? M_TEXT : M_DIM);
    }
    x += RADAR_FRAMES * 8 + 6;
  }
  if (g_state == RState::Unreadable) {
    const int16_t w = drawText(s, "Bad radar data", x, base, Font::F2, M_WARN);
    drawSep(s, x + w + 5, 226 + oy, M_DIM);
    drawText(s, "waiting for next frame", x + w + 10, base, Font::F2, M_MUTED);
    return;
  }
  int16_t rx = 310;
  const char *right = nullptr;
  uint8_t rc = M_MUTED;
  if (g_state == RState::Updating) right = "updating";
  else if (g_state == RState::Partial) { right = "partial coverage"; rc = M_WARN; }
  else if (g_state == RState::Stale) {
    snprintf(buf, sizeof(buf), "%ld min old", (long)((int32_t)(epochNow() - g_st.valid[g_st.n - 1]) / 60));
    right = buf;
    rc = M_WARN;
  }
  if (right) {
    rx -= drawText(s, right, 310, base, Font::F2, rc, R_BASELINE) + 8;
  } else if (g_st.echo) {                               // one continuous intensity bar, not when dry
    for (uint8_t k = 1; k <= 5; k++) s.fillRect(280 + (k - 1) * 6, 224 + oy, 6, 6, RAIN_SLOT[k]);
    rx = 272;
  }
  if (!g_st.rain) {   // "No rain" only when NOTHING is drawn (v3-R3-1)
    drawText(s, g_st.echo ? "Small echoes only" : "No rain within 50 mi", x, base, Font::F2, M_MUTED);
    return;
  }
  // words: always the NEWEST frame (header), named blobs only (docs/10 -> Words)
  char first[32], second[32] = "";
  if (g_st.rainMi < RADAR_CUE_MIN_MI) snprintf(first, sizeof(first), "Raining here");
  else if (g_st.heavy && g_st.heavyMi - g_st.rainMi <= 3.0f)
    snprintf(first, sizeof(first), "Heavy rain %d mi %s", (int)lroundf(g_st.rainMi), geo::compass8(g_st.rainAz));
  else snprintf(first, sizeof(first), "Rain %d mi %s", (int)lroundf(g_st.rainMi), geo::compass8(g_st.rainAz));
  if (g_st.heavy && g_st.heavyMi - g_st.rainMi > 3.0f)
    snprintf(second, sizeof(second), "heavy %d mi %s", (int)lroundf(g_st.heavyMi), geo::compass8(g_st.heavyAz));
  const int16_t w = drawText(s, first, x, base, Font::F2, g_state == RState::Stale ? M_MUTED : M_TEXT);
  setFont(s, Font::F2);
  if (second[0] && x + w + 10 + s.textWidth(second) <= rx) {
    drawSep(s, x + w + 5, 226 + oy, M_DIM);
    drawText(s, second, x + w + 10, base, Font::F2, M_MUTED);
  }
}

}  // namespace

void radarEnter() {
  g_mounted = sdAcquire();
  g_drawn = false;
  g_stVer = UINT32_MAX;
  g_fi = 0;
  g_frameMs = millis();
  g_dirty = true;
}

void radarUpdate(uint32_t now) {
  if (appRadarVersion() != g_stVer) {
    const uint8_t oldN = g_st.n;
    if (takeSnapshot() && g_st.n != oldN) g_fi = g_st.n ? g_st.n - 1 : 0;   // a new frame: newest first
  }
  const RState st = computeState();                      // ages change with the clock
  if (st != g_state) { g_state = st; g_dirty = true; }
  if (looping()) {                                       // 0.4 s per frame, the newest holds 2 s
    const uint32_t hold = g_fi == g_st.n - 1 ? 2000 : 400;
    if (now - g_frameMs >= hold) {
      g_frameMs = now;
      g_fi = (g_fi + 1) % g_st.n;
      g_dirty = true;
    }
  } else if (g_st.n) {
    g_fi = g_st.n - 1;
  }
  static uint32_t lastMinute = 0;
  if (now - lastMinute > 30000) { lastMinute = now; g_dirty = true; }   // ages / "updating"
  if (!g_dirty) return;
  g_dirty = false;

  buildPalette(g_state == RState::Stale);
  TFT_eSprite &s = *band;
  s.createPalette(g_pal, 16);
  File32 f;
  bool locked = false;
  const bool rain = g_st.n && g_state != RState::Offline && g_state != RState::NoSd;
  if (rain) {
    locked = sdLock(0);
    if (!locked && g_drawn) {   // the net task is converting (both file slots): keep the image, retry
      g_dirty = true;
      return;
    }
    if (locked) {
      char p[40];
      radarFramePath(g_st.valid[looping() ? g_fi : g_st.n - 1], p, sizeof(p));
      f = sdFs().open(p, O_RDONLY);
    }
    if (!f) g_dirty = true;     // first draw while busy: the basemap now, the rain next tick
  }
  for (int16_t y0 = 0; y0 < RADAR_H; y0 += BAND_H) {
    drawBand(s, y0, f ? &f : nullptr);
    drawStrip(s, y0);
    s.pushSprite(0, y0);
  }
  if (f) f.close();
  if (locked) sdUnlock();
  g_drawn = true;
}

void radarLeave() {
  if (g_mounted) sdRelease();
  g_mounted = false;
}

bool radarTouchBack(int16_t x, int16_t y) { return x < 48 && y < 36; }

// The weather screen's rain cue (docs/10 -> Rain cue): same words as the strip.
bool radarCue(char *out, size_t n) {
  RadarStatus r;
  appGetRadar(r);
  if (!r.sdOk || !r.n || !r.rain) return false;
  const uint32_t now = (uint32_t)time(nullptr);
  if (now < 1700000000u || (int32_t)(now - r.valid[r.n - 1]) > (int32_t)(RADAR_CUE_MAX_AGE_MIN * 60)) return false;
  if (r.rainMi < RADAR_CUE_MIN_MI) snprintf(out, n, "Raining here");
  else if (r.rainMi <= RADAR_CUE_MAX_MI)
    snprintf(out, n, "Rain %d mi %s", (int)lroundf(r.rainMi), geo::compass8(r.rainAz));
  else return false;
  return true;
}
