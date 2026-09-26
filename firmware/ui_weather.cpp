// Weather screen - layout from screens.py -> weather(); rules in 06 §3.
#include "aircraft_names.h"
#include "chip.h"
#include "geo.h"
#include "today_store.h"
#include "ui_internal.h"

using namespace ui;

namespace {

uint32_t g_wxVer = UINT32_MAX;
int      g_lastMinute = -1;
bool     g_lastStale = false;
char     g_chip[64] = "";
char     g_date[20] = "";
char     g_cue[32] = "";              // the rain cue on screen ("" = none)
uint32_t g_radarVer = UINT32_MAX;
char     g_head[96] = "";             // header signature: redrawn only when its words change
ChipMsg  g_chipMsg;                   // what the chip says now (the tap reads it)
uint32_t g_chipPassEndEpoch = 0;

int16_t f2w(const char *s) {
  setFont(*tft, Font::F2);
  return tft->textWidth(s);
}

bool isStale(const Weather &w) {
  if (!w.valid) return true;
  const time_t now = time(nullptr);
  return now > 1700000000 && now - w.fetchedEpoch > WEATHER_STALE_S;
}

void drawHeader(const Weather &w, bool force = true) {
  TFT_eSPI &g = *tft;
  static const char *DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  static const char *MONS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                               "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  struct tm lt;
  g_date[0] = '\0';
  if (localNow(lt)) snprintf(g_date, sizeof(g_date), "%s  %s %d", DAYS[lt.tm_wday], MONS[lt.tm_mon], lt.tm_mday);
  // Status only when degraded - no permanent clutter (R1-15).
  char status[32] = "";
  if (!w.valid) {
    snprintf(status, sizeof(status), "Weather unavailable");
  } else if (isStale(w)) {
    const long mins = (time(nullptr) - w.fetchedEpoch) / 60;
    if (mins < 120) snprintf(status, sizeof(status), "Updated %ld min ago", mins);
    else snprintf(status, sizeof(status), "Updated %ld h ago", mins / 60);
  }
  // The Today entry (docs/11): "· 31 overhead ›" after the date, clear of the status.
  // Before the clock syncs its text is hidden; the header target stays live (main.cpp).
  char entry[16] = "";
  const int16_t dateEnd = 10 + (g_date[0] ? f2w(g_date) : 0);
  const int16_t statusLeft = status[0] ? 296 - f2w(status) : SCREEN_W;
  if (g_date[0] && todayClockValid()) {
    headerEntryFit(todayOverhead(), dateEnd, statusLeft, f2w, entry);
  }
  char sig[96];
  snprintf(sig, sizeof(sig), "%s|%s|%s", g_date, status, entry);
  if (!force && strcmp(sig, g_head) == 0) return;
  snprintf(g_head, sizeof(g_head), "%s", sig);
  g.fillRect(0, 0, SCREEN_W, 24, COL_BG);
  if (g_date[0]) drawText(g, g_date, 10, 17, Font::F2, COL_MUTED);
  if (entry[0]) {
    drawSep(g, dateEnd + 8, 12, COL_DIM);
    const int16_t ew = drawText(g, entry, dateEnd + HEADER_SEP, 17, Font::F2, COL_MUTED);
    drawChevron(g, dateEnd + HEADER_SEP + ew + 5, 12, COL_MUTED);
  }
  if (status[0]) {
    drawText(g, status, 296, 17, Font::F2, COL_WARN, R_BASELINE);
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
    const int16_t cw = drawText(g, wxLabel(w.code, w.isDay), 82, 104, Font::Fs9, COL_MUTED);
    // Rain cue (docs/10): its own slot, right-aligned under the sunset line; the condition
    // is never dropped. fs9, else f2 when it would come within 8 px of the condition.
    if (g_cue[0]) {
      Font f = Font::Fs9;
      setFont(g, f);
      if (306 - (g.textWidth(g_cue) + 11) < 82 + cw + 8) f = Font::F2;
      drawText(g, g_cue, 306 - 11, 104, f, COL_RAIN, R_BASELINE);
      drawChevron(g, 306 - 6, 99, COL_RAIN);
    }
  } else {
    drawText(g, "--", 80, 82, Font::Fsb24, COL_MUTED);
    drawText(g, "No data yet", 82, 104, Font::Fs9, COL_MUTED);
    NetStatus n;                         // why: shown on screen (there may be no serial link)
    appGetNet(n);
    char diag[72];
    snprintf(diag, sizeof(diag), "%s  heap %u/%u/%uk", n.wxErr[0] ? n.wxErr : "wx: not tried yet", n.heapFreeK,
             n.heapMinK, n.heapLargestK);
    g.fillRect(0, 112, SCREEN_W, 40, COL_BG);
    drawText(g, diag, 10, 130, Font::F2, COL_WARN);
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

// Traffic chip: chipMessage() (chip.cpp, host-tested) decides; redrawn only when its text
// changes. Every state opens the map (docs/11, round 1 M1).
void drawChip(const Traffic &t, bool trafficUp) {
  ChipInputs in{};
  in.trafficUp = trafficUp;
  in.retryS = max<int32_t>(0, (int32_t)(t.nextRetryMs - millis()) / 1000);
  const Aircraft *nearest = nullptr;
  for (uint8_t i = 0; i < t.n; i++) {
    if (t.ac[i].onGround || t.ac[i].distNm > NEARBY_NM) continue;   // polls reach 25 nm (V4); "nearby" is 12
    if (!nearest) nearest = &t.ac[i];
    in.nNearby++;
  }
  const char *mfr, *model;
  if (nearest) {
    in.nearestType = typeLookup(nearest->type, mfr, model) ? model : (nearest->type[0] ? nearest->type : "Aircraft");
    in.nearestMi = nearest->distNm * 1.15078f;
    in.nearestDir = geo::compass8(nearest->azDeg);
  }
  PassRec p;                                  // just the last pass, on the loop stack
  const time_t nowE = time(nullptr);
  if (todayClockValid() && todayLastPass(p) && p.closeEpoch && nowE >= (time_t)p.closeEpoch) {
    in.hasPass = true;
    in.passAgeS = (uint32_t)(nowE - p.closeEpoch);
    in.passOp = p.lab.op;
    in.passType = p.lab.type;
    in.passIcao = p.lab.icao;
    in.passCode = p.lab.code;
    in.passHex = p.hex;
    g_chipPassEndEpoch = p.closeEpoch + TODAY_PASSED_S;
  }
  chipMessage(in, f2w, g_chipMsg);
  const ChipMsg &m = g_chipMsg;
  const bool offline = m.kind == ChipKind::Offline;
  char sig[64];
  snprintf(sig, sizeof(sig), "%s|%s", m.left, m.right);
  if (strcmp(sig, g_chip) == 0) return;
  strncpy(g_chip, sig, sizeof(g_chip));

  TFT_eSPI &g = *tft;
  g.fillRoundRect(6, 215, 308, 23, 11, COL_PANEL2);
  drawPlaneGlyph(g, 21, 226, 45, 0.55f, offline ? COL_DIM : COL_PLANE);
  drawText(g, m.left, 36, 231, Font::F2, offline ? COL_WARN : COL_TEXT);
  if (offline) {
    drawText(g, m.right, 304, 231, Font::F2, COL_MUTED, R_BASELINE);
  } else {
    drawText(g, m.right, 296, 231, Font::F2, COL_MUTED, R_BASELINE);
    drawChevron(g, 302, 226, COL_MUTED);
  }
}

}  // namespace

void weatherEnter() {
  tft->fillScreen(COL_BG);
  g_wxVer = UINT32_MAX;
  g_lastMinute = -1;
  g_chip[0] = '\0';
  g_head[0] = '\0';
}

bool weatherChipPassed(char hexOut[7], char labelOut[48], uint32_t &untilMs) {
  if (g_chipMsg.kind != ChipKind::Passed || !g_chipMsg.focusHex[0]) return false;
  snprintf(hexOut, 7, "%s", g_chipMsg.focusHex);
  snprintf(labelOut, 48, "%s", g_chipMsg.right);
  const time_t nowE = time(nullptr);
  const int32_t left = (int32_t)(g_chipPassEndEpoch - (uint32_t)nowE);
  untilMs = millis() + (uint32_t)max<int32_t>(0, left) * 1000UL;
  return true;
}

void weatherUpdate(const Weather &w, const Traffic &t, bool trafficUp, uint32_t now) {
  (void)now;
  struct tm lt;
  const int minute = localNow(lt) ? lt.tm_hour * 60 + lt.tm_min : -2;
  const bool stale = isStale(w);

  bool cueChanged = false;
  if (appRadarVersion() != g_radarVer || minute != g_lastMinute) {   // frames arrive / age out
    g_radarVer = appRadarVersion();
    char cue[32] = "";
    radarCue(cue, sizeof(cue));
    cueChanged = strcmp(cue, g_cue) != 0;
    if (cueChanged) snprintf(g_cue, sizeof(g_cue), "%s", cue);
  }
  if (w.version != g_wxVer || cueChanged) {
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
    if (!w.valid) drawBody(w);           // refresh the diagnostics line
    if (stale != g_lastStale) {
      g_lastStale = stale;
      drawBody(w);
    }
  }
  drawChip(t, trafficUp);
  static uint32_t todayVer = 0;               // the header's "31 overhead" follows the log
  if (todayVersion() != todayVer) {
    todayVer = todayVersion();
    drawHeader(w, false);                     // redraws only if its words changed
  }
}
