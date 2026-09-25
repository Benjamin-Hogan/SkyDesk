// Plane screen - layout from screens.py -> plane(); rules in 06 §4.
#include "aircraft_names.h"
#include "geo.h"
#include "settings.h"
#include "ui_internal.h"

#include <math.h>

using namespace ui;

namespace {

// Dome geometry (screen coords). The sprite sits at (DOME_X, DOME_Y).
constexpr int16_t DOME_CX = 82, DOME_CY = 124, DOME_R = 70;
constexpr int16_t DOME_X = DOME_CX - 72, DOME_Y = DOME_CY - 72;   // 144x144 sprite
constexpr int16_t SC = 72;                                        // centre inside sprite
constexpr int16_t COL_X = 166, COL_W = 148;
constexpr int16_t LOOK_Y = 50;                                    // LOOK sprite top
constexpr int16_t STATS_Y = 202;

char     g_headerSig[96] = "";
char     g_routeSig[64] = "";
uint32_t g_trafficVer = UINT32_MAX;
uint32_t g_lastSlowTick = 0;
uint32_t g_arrivalUntil = 0;
int16_t  g_lastViewUp = -1;

struct Sky { float az, el; };

Sky skyOf(double lat, double lon, double altFt) {
  double d, az;
  geo::distBearing({OBS_LAT, OBS_LON}, {lat, lon}, d, az);
  return {(float)az, (float)geo::elevation(d, altFt, OBS_ELEV_FT)};
}

// Equidistant projection, sprite coords. clamp keeps the glyph 19 px inside the rim.
void domeXY(float az, float el, float viewUp, bool clamp, float &x, float &y) {
  float rr = DOME_R * (90.0f - fmaxf(0.0f, el)) / 90.0f;
  if (clamp) rr = fminf(rr, DOME_R - 19);
  const float th = (az - viewUp) * DEG_TO_RAD;
  x = SC + rr * sinf(th);
  y = SC - rr * cosf(th);
}

uint32_t arcAngle(float a) {   // TFT_eSPI arcs: clockwise from 6 o'clock, 0..360
  int v = (int)lroundf(a) % 360;
  return (uint32_t)(v < 0 ? v + 360 : v);
}

// ---------------------------------------------------------------------------
//  Header (direct draw; redrawn only when its content changes)
// ---------------------------------------------------------------------------
void drawHeader(const TrackView &v, const PlaneLabels &L, uint32_t now) {
  TFT_eSPI &g = *tft;
  char pill[20] = "";
  uint16_t pillCol = COL_MUTED;
  bool pillF2 = false;
  if (v.mode == PlaneMode::Departing) snprintf(pill, sizeof(pill), "LEAVING");
  else if (v.mode == PlaneMode::Forced) snprintf(pill, sizeof(pill), "NOT OVERHEAD");
  else if (v.extra > 0) {
    snprintf(pill, sizeof(pill), "+%d more", v.extra);
    pillCol = COL_PLANE;
    pillF2 = true;
  }
  const bool dimBar = v.mode == PlaneMode::Departing || v.stale;

  char sig[96];
  snprintf(sig, sizeof(sig), "%s|%s|%s|%s|%s|%s|%d", v.ac.hex, L.op, L.type, L.line2a, L.line2b, pill, dimBar);
  if (strcmp(sig, g_headerSig) != 0) {
    strncpy(g_headerSig, sig, sizeof(g_headerSig));
    g.fillRect(0, 0, SCREEN_W, 44, COL_PANEL);

    int16_t right = 314;
    if (pill[0]) {
      const Font pf = pillF2 ? Font::F2 : Font::Glcd;
      setFont(g, pf);
      const int16_t w = g.textWidth(pill) + 16;
      g.drawRoundRect(right - w, 8, w, 20, 10, pillCol);
      drawText(g, pill, right - w / 2, pillF2 ? 22 : 21, pf, pillCol, C_BASELINE);
      right -= w + 8;
    }

    // Line 1 fitted as ONE line: fsb12 -> fsb9, then truncate the operator (06 §4.1b).
    const int16_t avail = right - 12;
    const int16_t sep = L.op[0] ? 14 : 0;
    Font f = Font::Fsb9;
    for (Font cand : {Font::Fsb12, Font::Fsb9}) {
      setFont(g, cand);
      if ((L.op[0] ? g.textWidth(L.op) : 0) + sep + g.textWidth(L.type) <= avail) { f = cand; break; }
    }
    setFont(g, f);
    const int16_t typeW = g.textWidth(L.type);
    int16_t x = 12;
    if (L.op[0]) {
      x += drawFit(g, L.op, x, 24, avail - sep - typeW, COL_TEXT, &f, 1) + 6;
      drawSep(g, x, 24 - (f == Font::Fsb12 ? 6 : 4), COL_MUTED, 2);
      x += 8;
    }
    drawFit(g, L.type, x, 24, right - x, COL_TEXT, &f, 1);

    // Line 2: flight . registration
    x = 12;
    x += drawText(g, L.line2a, x, 39, Font::F2, COL_MUTED);
    if (L.line2b[0] && L.airline) {
      drawSep(g, x + 4, 34, COL_DIM);
      drawText(g, L.line2b, x + 10, 39, Font::F2, COL_MUTED);
    }
  }

  // Left accent bar: flashes on arrival (06 §6), dims when not confident.
  uint16_t bar = dimBar ? COL_PLANE_DIM : COL_PLANE;
  if ((int32_t)(g_arrivalUntil - now) > 0 && ((g_arrivalUntil - now) / 150) % 2) bar = COL_PANEL;
  g.fillRect(0, 0, 4, 44, bar);

  // Forced countdown on line 2 (Font 2 paints its own background: no flicker).
  if (v.mode == PlaneMode::Forced) {
    char buf[24];
    snprintf(buf, sizeof(buf), "weather in %d s", v.forcedLeftS);
    setFont(g, Font::F2);
    g.setTextColor(COL_MUTED, COL_PANEL);
    g.setTextDatum(R_BASELINE);
    g.setTextPadding(g.textWidth("weather in 88 s"));
    g.drawString(buf, 314, 39);
    g.setTextPadding(0);
  }
}

// ---------------------------------------------------------------------------
//  Sky dome (16-bit sprite, pushed with COL_BG transparent so the labels drawn
//  around it on the TFT survive every update - R3 required change)
// ---------------------------------------------------------------------------
void drawDome(const TrackView &v, float viewUp) {
  TFT_eSprite &s = *dome;
  const Aircraft &a = v.ac;
  const bool confident = (v.mode == PlaneMode::Live && !v.stale) || v.mode == PlaneMode::Forced;
  const uint16_t accent = confident ? COL_PLANE : COL_PLANE_DIM;
  const bool overhead = a.elDeg >= OVERHEAD_EL_DEG;

  s.fillSprite(COL_BG);
  s.fillCircle(SC, SC, DOME_R, COL_PANEL);
  const float look = a.azDeg - viewUp + 180.0f;
  if (!overhead)   // faint "slice of sky" sector (ir = 0)
    s.drawArc(SC, SC, DOME_R, 0, arcAngle(look - 14), arcAngle(look + 14), COL_PLANE_FAINT, COL_PANEL, false);
  // dashed elevation rings at 30 / 60 deg
  for (int ring : {DOME_R * 2 / 3, DOME_R / 3}) {
    const int n = (int)(2 * PI * ring);
    for (int i = 0; i < n; i++)
      if (i % 5 < 2) {
        const float t = 2 * PI * i / n;
        s.drawPixel(SC + lroundf(ring * cosf(t)), SC + lroundf(ring * sinf(t)), COL_HAIR);
      }
  }
  s.drawCircle(SC, SC, DOME_R, COL_HAIR);
  if (!overhead) {
    s.drawArc(SC, SC, DOME_R + 1, DOME_R - 4, arcAngle(look - 14), arcAngle(look + 14), accent, COL_PANEL, true);
  } else {
    s.drawCircle(SC, SC, 21, accent);
    s.drawCircle(SC, SC, 20, accent);
  }

  float gx, gy;
  domeXY(a.azDeg, a.elDeg, viewUp, true, gx, gy);

  // Compass letters (rotate with VIEW_UP), skipped under the glyph.
  const char *labs[] = {"N", "E", "S", "W"};
  for (int i = 0; i < 4; i++) {
    const float th = (i * 90 - viewUp) * DEG_TO_RAD;
    const float lx = SC + (DOME_R - 11) * sinf(th), ly = SC - (DOME_R - 11) * cosf(th);
    if (hypotf(lx - gx, ly - gy) < 16) continue;
    drawText(s, labs[i], lroundf(lx), lroundf(ly) + 5, Font::F2, i == 0 ? COL_TEXT : COL_DIM, C_BASELINE);
  }
  if (hypotf(gx - SC, gy - SC) > 20) {
    s.fillCircle(SC, SC, 2, COL_MUTED);
    drawText(s, "UP", SC + 5, SC + 11, Font::Glcd, COL_DIM);
  }

  // Other qualifying aircraft: hollow dots.
  for (uint8_t i = 0; i < v.nOthers; i++) {
    float ox, oy;
    domeXY(v.others[i].azDeg, v.others[i].elDeg, viewUp, false, ox, oy);
    s.drawCircle(lroundf(ox), lroundf(oy), 4, COL_MUTED);
    s.drawCircle(lroundf(ox), lroundf(oy), 3, COL_MUTED);
  }

  // Direction arrow: fixed length, direction from TRUE positions now -> +60 s.
  float heading = a.track >= 0 ? a.track - viewUp : 0;
  if (!v.stale && a.track >= 0 && a.gsKt > 5) {
    const geo::LatLon fut = geo::destination({a.lat, a.lon}, a.track, a.gsKt * 0.514444 * TRAIL_S);
    const Sky f = skyOf(fut.lat, fut.lon, a.altFt + a.vRateFpm * (TRAIL_S / 60.0f));
    float rx, ry, fx, fy;
    domeXY(a.azDeg, a.elDeg, viewUp, false, rx, ry);
    domeXY(f.az, f.el, viewUp, false, fx, fy);
    const float L = hypotf(fx - rx, fy - ry);
    if (L >= 3) {
      const float ux = (fx - rx) / L, uy = (fy - ry) / L;
      heading = atan2f(ux, -uy) * RAD_TO_DEG;   // glyph points where it moves on the dome
      const float bx = gx - SC, by = gy - SC, bdu = bx * ux + by * uy;
      const float room = -bdu + sqrtf(fmaxf(0, bdu * bdu - (bx * bx + by * by - (DOME_R - 3) * (DOME_R - 3))));
      const float tip = fminf(40, room);
      if (tip >= 24) {
        const float hx = gx + ux * (tip - 8), hy = gy + uy * (tip - 8);
        s.drawWideLine(gx + ux * 12, gy + uy * 12, hx, hy, 2, accent, COL_PANEL);
        s.fillTriangle(lroundf(gx + ux * tip), lroundf(gy + uy * tip), lroundf(hx - uy * 5), lroundf(hy + ux * 5),
                       lroundf(hx + uy * 5), lroundf(hy - ux * 5), accent);
      } else if (tip >= 14) {   // not enough room for a shaft: just the head
        const float hx = gx + ux * (tip - 8), hy = gy + uy * (tip - 8);
        s.fillTriangle(lroundf(gx + ux * tip), lroundf(gy + uy * tip), lroundf(hx - uy * 5), lroundf(hy + ux * 5),
                       lroundf(hx + uy * 5), lroundf(hy - ux * 5), accent);
      }
    }
  }
  drawPlaneGlyph(s, gx, gy, heading, 1.0f, accent);
  s.pushSprite(DOME_X, DOME_Y, COL_BG);
}

void drawDomeChrome(float viewUp, const TrackView &v) {
  TFT_eSPI &g = *tft;
  drawText(g, "HORIZON", 4, 199, Font::Glcd, COL_DIM);
  if (viewUp != 0) {
    g.fillTriangle(DOME_CX - 5, DOME_CY - DOME_R - 8, DOME_CX + 5, DOME_CY - DOME_R - 8, DOME_CX,
                   DOME_CY - DOME_R - 1, COL_TEXT);
    drawText(g, "AHEAD", DOME_CX + 9, DOME_CY - DOME_R - 1, Font::Glcd, COL_MUTED);
  }
}

// ---------------------------------------------------------------------------
//  LOOK block (4-bit sprite at COL_X, LOOK_Y)
// ---------------------------------------------------------------------------
void drawLook(const TrackView &v, float viewUp) {
  TFT_eSprite &s = *look;
  const Aircraft &a = v.ac;
  const bool confident = (v.mode == PlaneMode::Live && !v.stale) || v.mode == PlaneMode::Forced;
  const uint8_t accent = confident ? P_PLANE : P_PLANE_DIM;
  const uint8_t body = confident ? P_TEXT : P_MUTED;
  const bool rel = viewUp != 0;
  const int16_t y0 = rel ? 93 : 97, y1 = rel ? 123 : 130;
  const int16_t oy = LOOK_Y;

  s.fillSprite(P_BG);
  drawText(s, "LOOK", 1, (rel ? 58 : 60) - oy, Font::Glcd, P_DIM);
  const char *line3;
  char fists[24];
  if (a.elDeg >= OVERHEAD_EL_DEG) {
    drawText(s, "UP", -1, y0 - oy, Font::Fsb24, accent);
    drawText(s, "overhead", 1, y1 - 3 - oy, Font::Fsb12, body);
    line3 = "lean back";
  } else {
    drawText(s, geo::compass8(a.azDeg), -1, y0 - oy, Font::Fsb24, accent);
    const int16_t w = drawDegrees(s, (int)lroundf(a.elDeg), 0, y1 - oy, Font::Fsb18, body);
    drawText(s, "up", w + 5, y1 - oy, Font::Fsb18, body);
    if (a.elDeg < 10) {
      line3 = "near horizon";
    } else {
      const int f = (int)lroundf(a.elDeg / 10);
      snprintf(fists, sizeof(fists), "about %d fist%s", f, f > 1 ? "s" : "");
      line3 = fists;
    }
  }
  char dist[16];
  snprintf(dist, sizeof(dist), "%.1f mi", a.distNm * 1.15078f);
  if (v.stale) {
    char buf[32];
    snprintf(buf, sizeof(buf), "last seen %d s ago", v.ageS);
    drawText(s, buf, 1, 149 - oy, Font::F2, P_WARN);
  } else if (rel) {
    drawText(s, geo::relativeWords(a.azDeg, viewUp), 0, 150 - oy, Font::Fsb12, body);
  } else {
    const int16_t w = drawText(s, line3, 1, 149 - oy, Font::F2, P_MUTED);
    drawSep(s, w + 6, 144 - oy, P_DIM);
    drawText(s, dist, w + 11, 149 - oy, Font::F2, P_MUTED);
  }
  s.drawFastHLine(0, 157 - oy, COL_W, P_HAIR);
  s.pushSprite(COL_X, LOOK_Y);

  if (rel) {   // distance lives in the dome's top-left corner in you-relative mode
    TFT_eSPI &g = *tft;
    setFont(g, Font::F2);
    g.setTextColor(COL_MUTED, COL_BG);
    g.setTextDatum(L_BASELINE);
    g.setTextPadding(g.textWidth("88.8 mi"));
    g.drawString(dist, 4, 58);
    g.setTextPadding(0);
  }
}

// ---------------------------------------------------------------------------
//  Route block (direct draw, only on change)
// ---------------------------------------------------------------------------
struct CityShort { const char *from, *to; };
const CityShort CITY_SHORT[] = {
  {"Dallas-Fort Worth", "Dallas"}, {"Salt Lake City", "Salt Lake"}, {"Minneapolis", "Mpls"},
  {"San Francisco", "San Fran"}, {"Washington", "Wash."}, {"Philadelphia", "Philly"},
  {"Fort Lauderdale", "Ft Laud."}, {"Colorado Springs", "Colo Spgs"}, {"Albuquerque", "Albuq."},
};

void shortCity(char *c) {
  for (const auto &cs : CITY_SHORT)
    if (strcmp(c, cs.from) == 0) { strcpy(c, cs.to); return; }
}

void fitCities(TFT_eSPI &g, char *oc, char *dc, int16_t avail) {
  setFont(g, Font::F2);
  if (g.textWidth(oc) + g.textWidth(dc) <= avail) return;
  shortCity(oc);
  shortCity(dc);
  // Trim the longer name, ending it with '.'; stops at 2 chars (R3 nice-to-have 4).
  while (g.textWidth(oc) + g.textWidth(dc) > avail) {
    char *t = strlen(oc) >= strlen(dc) ? oc : dc;
    size_t n = strlen(t);
    if (n <= 2) break;
    t[n - 2] = '\0';
    n -= 2;
    while (n > 0 && (t[n - 1] == ' ' || t[n - 1] == '.')) t[--n] = '\0';
    t[n] = '.';
    t[n + 1] = '\0';
  }
}

void drawRoute(const Aircraft &a, const RouteInfo *r, bool looked) {
  const bool has = r && r->hasRoute;
  const bool ok = has && routePlausible(*r, a);
  const bool airlineCs = strlen(a.callsign) >= 4 && isalpha(a.callsign[0]) && isalpha(a.callsign[2]) &&
                         isdigit(a.callsign[3]);
  char sig[64];
  snprintf(sig, sizeof(sig), "%s|%d|%d|%d|%s%s", a.hex, looked, has, ok, has ? r->origIata : "",
           has ? r->destIata : "");
  if (strcmp(sig, g_routeSig) == 0) return;
  strncpy(g_routeSig, sig, sizeof(g_routeSig));

  TFT_eSPI &g = *tft;
  g.fillRect(COL_X, 159, SCREEN_W - COL_X, 42, COL_BG);
  const int16_t x = COL_X;
  if (has) {
    const uint16_t c = ok ? COL_TEXT : COL_DIM;
    const int16_t w = drawText(g, r->origIata, x, 181, Font::Fsb12, c);
    drawArrowRight(g, x + w + 5, 175, 18, c);
    drawText(g, r->destIata, x + w + 28, 181, Font::Fsb12, c);
    if (ok) {
      char oc[22], dc[22];
      strncpy(oc, r->origCity, sizeof(oc));
      strncpy(dc, r->destCity, sizeof(dc));
      fitCities(g, oc, dc, COL_W - 1 - 18);
      const int16_t cw = drawText(g, oc, x + 1, 197, Font::F2, COL_MUTED);
      drawArrowRight(g, x + cw + 4, 192, 10, COL_MUTED, 3, true);
      drawText(g, dc, x + cw + 18, 197, Font::F2, COL_MUTED);
    } else {
      const char *lab = "ROUTE UNVERIFIED";
      setFont(g, Font::Glcd);
      g.drawRoundRect(x, 187, g.textWidth(lab) + 10, 13, 3, COL_WARN);
      drawText(g, lab, x + 5, 197, Font::Glcd, COL_WARN);
    }
  } else if (!looked) {
    drawText(g, "Looking up route", x, 180, Font::Fs9, COL_MUTED);
  } else if (airlineCs) {
    drawText(g, "Route unknown", x, 180, Font::Fs9, COL_MUTED);
    drawText(g, "no schedule found", x + 1, 197, Font::F2, COL_DIM);
  } else {
    drawText(g, "Private flight", x, 180, Font::Fs9, COL_MUTED);
    drawText(g, "no route filed", x + 1, 197, Font::F2, COL_DIM);
  }
}

// ---------------------------------------------------------------------------
//  Stats bar (4-bit sprite at 0, STATS_Y)
// ---------------------------------------------------------------------------
void drawStats(const TrackView &v) {
  TFT_eSprite &s = *stats;
  const Aircraft &a = v.ac;
  const int16_t oy = STATS_Y;
  s.fillSprite(P_PANEL);
  if (v.mode == PlaneMode::Departing && v.graceTotalS) {
    s.fillRect(0, 0, 320 * v.graceLeftS / v.graceTotalS, 3, P_PLANE_DIM);
  } else if (v.mode == PlaneMode::Forced) {
    s.fillRect(0, 0, 320 * v.forcedLeftS / FORCED_SHOW_S, 3, P_MUTED);
  }
  char buf[16];
  // ALTITUDE with thousands separator
  const long alt = lroundf(a.altFt / 100.0f) * 100;
  if (alt >= 1000) snprintf(buf, sizeof(buf), "%ld,%03ld", alt / 1000, alt % 1000);
  else snprintf(buf, sizeof(buf), "%ld", alt);
  drawText(s, "ALTITUDE", 10, 215 - oy, Font::Glcd, P_DIM);
  int16_t w = drawNumber(s, buf, 10, 234 - oy, Font::Fsb9, P_TEXT);
  drawText(s, "ft", 10 + w + 3, 234 - oy, Font::F2, P_MUTED);

  snprintf(buf, sizeof(buf), "%d", (int)lroundf(a.gsKt * 1.15078f));
  drawText(s, "SPEED", 112, 215 - oy, Font::Glcd, P_DIM);
  w = drawNumber(s, buf, 112, 234 - oy, Font::Fsb9, P_TEXT);
  drawText(s, "mph", 112 + w + 3, 234 - oy, Font::F2, P_MUTED);

  const int16_t x = 196;
  const char *word;
  if (a.vRateFpm > MIN_VRATE_FPM) {
    s.fillTriangle(x, 233 - oy, x + 12, 233 - oy, x + 6, 221 - oy, P_TEXT);
    word = "Climbing";
  } else if (a.vRateFpm < -MIN_VRATE_FPM) {
    s.fillTriangle(x, 222 - oy, x + 12, 222 - oy, x + 6, 234 - oy, P_TEXT);
    word = "Descending";
  } else {
    s.fillRect(x, 226 - oy, 12, 3, P_TEXT);
    word = "Level";
  }
  drawText(s, word, x + 17, 234 - oy, Font::Fs9, P_TEXT);
  s.pushSprite(0, STATS_Y);
}

}  // namespace

void planeEnter() {
  tft->fillScreen(COL_BG);
  g_headerSig[0] = '\0';
  g_routeSig[0] = '\0';
  g_trafficVer = UINT32_MAX;
  g_lastViewUp = -1;
}

void planeUpdate(const TrackView &v, const Traffic &t, uint32_t now, bool arrival) {
  if (arrival) g_arrivalUntil = now + 600;
  const float viewUp = settings().viewUpDeg;

  RouteInfo r;
  const bool looked = routeGet(v.ac.hex, r);
  PlaneLabels L;
  planeLabels(v.ac, looked ? &r : nullptr, L);
  drawHeader(v, L, now);   // cheap when unchanged (signature compare)

  if (g_lastViewUp != (int16_t)viewUp) {
    g_lastViewUp = (int16_t)viewUp;
    tft->fillRect(0, 44, COL_X - 4, 158, COL_BG);
    drawDomeChrome(viewUp, v);
    g_trafficVer = UINT32_MAX;
  }

  // Sprites: on every traffic update, and once a second for countdowns / age.
  const bool slowTick = now - g_lastSlowTick >= 1000;
  if (t.version != g_trafficVer || slowTick || arrival) {
    g_trafficVer = t.version;
    g_lastSlowTick = now;
    drawDome(v, viewUp);
    drawLook(v, viewUp);
    drawRoute(v.ac, looked ? &r : nullptr, looked);
    drawStats(v);
  }
}
