// Weather screen - layout from screens.py -> weather(); rules in 06 §3.
#include "aircraft_names.h"
#include "geo.h"
#include "ui_internal.h"

using namespace ui;

namespace {

uint32_t g_wxVer = UINT32_MAX;
int      g_lastMinute = -1;
bool     g_lastStale = false;
char     g_chip[64] = "";
char     g_date[20] = "";

bool isStale(const Weather &w) {
  if (!w.valid) return true;
  const time_t now = time(nullptr);
  return now > 1700000000 && now - w.fetchedEpoch > WEATHER_STALE_S;
}

void drawHeader(const Weather &w) {
  TFT_eSPI &g = *tft;
  g.fillRect(0, 0, SCREEN_W, 24, COL_BG);
  static const char *DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  static const char *MONS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                               "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  struct tm lt;
  if (localNow(lt)) {
    snprintf(g_date, sizeof(g_date), "%s  %s %d", DAYS[lt.tm_wday], MONS[lt.tm_mon], lt.tm_mday);
    drawText(g, g_date, 10, 17, Font::F2, COL_MUTED);
  }
  // Status only when degraded - no permanent clutter (R1-15).
  if (!w.valid) {
    drawText(g, "Weather unavailable", 296, 17, Font::F2, COL_WARN, R_BASELINE);
    g.fillCircle(305, 12, 3, COL_WARN);
  } else if (isStale(w)) {
    char buf[32];
    const long mins = (time(nullptr) - w.fetchedEpoch) / 60;
    if (mins < 120) snprintf(buf, sizeof(buf), "Updated %ld min ago", mins);
    else snprintf(buf, sizeof(buf), "Updated %ld h ago", mins / 60);
    drawText(g, buf, 296, 17, Font::F2, COL_WARN, R_BASELINE);
    g.fillCircle(305, 12, 3, COL_WARN);
  }
}

void drawClock(const Weather &w) {
  TFT_eSprite &s = *clockSpr;         // at (196, 32), 120 x 58, 4-bit
  s.fillSprite(P_BG);
  struct tm lt;
  char hm[8] = "--:--", ap[4] = "";
  if (localNow(lt)) fmtClock(lt, hm, sizeof(hm), ap, sizeof(ap));
  setFont(s, Font::F2);
  const int16_t wPM = s.textWidth("PM");
  drawText(s, hm, 110 - wPM - 4, 30, Font::Fsb18, P_TEXT, R_BASELINE);
  drawText(s, ap, 110 - wPM, 30, Font::F2, P_MUTED);

  // Next sun event.
  if (w.valid) {
    const time_t now = time(nullptr);
    time_t evt = 0;
    const char *lab = nullptr;
    if (now < w.sunrise[0]) { evt = w.sunrise[0]; lab = "Sunrise"; }
    else if (now < w.sunset[0]) { evt = w.sunset[0]; lab = "Sunset"; }
    else if (w.sunrise[1]) { evt = w.sunrise[1]; lab = "Sunrise"; }
    if (lab) {
      struct tm e;
      localtime_r(&evt, &e);
      char eh[8], ea[4], buf[24];
      fmtClock(e, eh, sizeof(eh), ea, sizeof(ea));
      snprintf(buf, sizeof(buf), "%s %s", lab, eh);
      drawText(s, buf, 110, 52, Font::F2, P_MUTED, R_BASELINE);
    }
  }
  s.pushSprite(196, 32);
}

void drawBody(const Weather &w) {
  TFT_eSPI &g = *tft;
  g.fillRect(0, 26, 196, 126, COL_BG);
  g.fillRect(196, 90, 124, 62, COL_BG);
  const bool stale = isStale(w);

  // Hero
  if (w.valid) {
    drawWxIcon(g, 40, 62, 1.5f, wxKind(w.code), w.isDay, COL_BG);
    drawDegrees(g, w.tempF, 80, 82, Font::Fsb24, stale ? COL_MUTED : COL_TEXT);
    drawText(g, wxLabel(w.code, w.isDay), 82, 104, Font::Fs9, COL_MUTED);
  } else {
    drawText(g, "--", 80, 82, Font::Fsb24, COL_MUTED);
    drawText(g, "No data yet", 82, 104, Font::Fs9, COL_MUTED);
    return;
  }

  // Detail cells: FEELS | HI / LO | HUMIDITY | WIND
  const int16_t xs[] = {10, 84, 170, 238};
  const char *labs[] = {"FEELS", "HI / LO", "HUMIDITY", "WIND"};
  for (int i = 0; i < 4; i++) drawText(g, labs[i], xs[i], 126, Font::Glcd, COL_DIM);
  drawDegrees(g, w.feelsF, xs[0], 145, Font::Fsb9, COL_TEXT);
  int16_t x = xs[1] + drawDegrees(g, w.hiF, xs[1], 145, Font::Fsb9, COL_TEXT);
  x += drawText(g, "/", x + 1, 145, Font::Fsb9, COL_DIM) + 2;
  drawDegrees(g, w.loF, x + 1, 145, Font::Fsb9, COL_MUTED);
  char buf[16];
  snprintf(buf, sizeof(buf), "%d%%", w.humidity);
  drawNumber(g, buf, xs[2], 145, Font::Fsb9, COL_TEXT);
  snprintf(buf, sizeof(buf), "%s %d", geo::compass8(w.windDeg), w.windMph);
  const int16_t ww = drawNumber(g, buf, xs[3], 145, Font::Fsb9, COL_TEXT);
  drawText(g, "mph", xs[3] + ww + 3, 145, Font::F2, COL_MUTED);

  // Hourly strip
  g.fillRect(0, 152, SCREEN_W, 60, COL_BG);
  g.fillRoundRect(6, 154, 308, 56, 6, COL_PANEL);
  for (uint8_t i = 0; i < w.nHourly && i < WX_HOURS; i++) {
    const HourSlot &h = w.hourly[i];
    const int16_t cx = 31 + i * 51;
    fmtHour(h.t, buf, sizeof(buf));
    drawText(g, buf, cx, 168, Font::F2, COL_MUTED, C_BASELINE);
    if (h.pop >= 20) {   // icon + precip% share the row, centred as a pair
      drawWxIcon(g, cx - 9, 181, 0.55f, wxKind(h.code), h.isDay, COL_PANEL);
      snprintf(buf, sizeof(buf), "%d%%", h.pop);
      drawText(g, buf, cx + 3, 186, Font::Glcd, COL_RAIN);
    } else {
      drawWxIcon(g, cx, 181, 0.55f, wxKind(h.code), h.isDay, COL_PANEL);
    }
    drawDegrees(g, h.tempF, cx, 205, Font::Fsb9, COL_TEXT, C_BASELINE);
  }
}

// Traffic chip: redrawn only when its text changes.
void drawChip(const Traffic &t, bool radarUp) {
  char left[24], right[40];
  bool offline = !radarUp;
  if (offline) {
    const int32_t s = max<int32_t>(0, (int32_t)(t.nextRetryMs - millis()) / 1000);
    snprintf(left, sizeof(left), "Radar offline");
    snprintf(right, sizeof(right), "retrying in %d s", (int)s);
  } else {
    int n = 0;
    const Aircraft *nearest = nullptr;
    for (uint8_t i = 0; i < t.n; i++) {
      if (t.ac[i].onGround) continue;
      if (!nearest) nearest = &t.ac[i];
      n++;
    }
    snprintf(left, sizeof(left), "%d nearby", n);
    if (nearest) {
      const char *mfr, *model;
      const char *type = typeLookup(nearest->type, mfr, model) ? model
                         : (nearest->type[0] ? nearest->type : "Aircraft");
      snprintf(right, sizeof(right), "%s  %.1f mi %s", type, nearest->distNm * 1.15078f,
               geo::compass8(nearest->azDeg));
    } else {
      snprintf(right, sizeof(right), "quiet skies");
    }
  }
  char sig[64];
  snprintf(sig, sizeof(sig), "%s|%s", left, right);
  if (strcmp(sig, g_chip) == 0) return;
  strncpy(g_chip, sig, sizeof(g_chip));

  TFT_eSPI &g = *tft;
  g.fillRoundRect(6, 215, 308, 23, 11, COL_PANEL2);
  drawPlaneGlyph(g, 21, 226, 45, 0.55f, offline ? COL_DIM : COL_PLANE);
  drawText(g, left, 36, 231, Font::F2, offline ? COL_WARN : COL_TEXT);
  if (offline) {
    drawText(g, right, 304, 231, Font::F2, COL_MUTED, R_BASELINE);
  } else {
    drawText(g, right, 296, 231, Font::F2, COL_MUTED, R_BASELINE);
    drawChevron(g, 302, 226, COL_MUTED);
  }
}

}  // namespace

void weatherEnter() {
  tft->fillScreen(COL_BG);
  g_wxVer = UINT32_MAX;
  g_lastMinute = -1;
  g_chip[0] = '\0';
}

void weatherUpdate(const Weather &w, const Traffic &t, bool radarUp, uint32_t now) {
  (void)now;
  struct tm lt;
  const int minute = localNow(lt) ? lt.tm_hour * 60 + lt.tm_min : -2;
  const bool stale = isStale(w);

  if (w.version != g_wxVer) {
    g_wxVer = w.version;
    drawBody(w);
    drawHeader(w);
    drawClock(w);
    g_lastMinute = minute;
    g_lastStale = stale;
  } else if (minute != g_lastMinute) {   // clock + "updated N min ago" once a minute
    g_lastMinute = minute;
    drawClock(w);
    drawHeader(w);
    if (stale != g_lastStale) {
      g_lastStale = stale;
      drawBody(w);
    }
  }
  drawChip(t, radarUp);
}
