// Today page (3.0, docs/11-today.md) - layout from docs/mockups/today_screen.py.
// Draws straight to the TFT (no sprite), cell by cell: a poll changes the nearby count,
// not the whole screen (round 1, S4).
#include "aircraft_names.h"
#include "sd_store.h"
#include "today_store.h"
#include "ui_internal.h"

using namespace ui;

namespace {

constexpr int16_t BAR_X0 = 16, BAR_W = 11, BAR_STEP = 12, BAR_BASE = 128, BAR_MAX = 30;
constexpr int16_t ROW_Y[3] = {177, 201, 225};
constexpr int16_t RIGHT_X = 310, GAP = 12;

uint32_t g_ver = UINT32_MAX;
int      g_minute = -1;
uint32_t g_sigHead, g_sigBars, g_sigList;   // hashes of each cell's words (RAM)
uint32_t g_sigOv, g_sigNb, g_sigRare;

uint32_t fnv(const char *s) {
  uint32_t h = 2166136261u;
  for (; *s; ++s) h = (h ^ (uint8_t)*s) * 16777619u;
  return h ? h : 1;                           // 0 = "never drawn"
}
bool     g_trafficUp = true;

uint32_t dayKeyOf(time_t t) {
  struct tm lt;
  localtime_r(&t, &lt);
  return (uint32_t)((lt.tm_year + 1900) * 10000 + (lt.tm_mon + 1) * 100 + lt.tm_mday);
}

int16_t f2w(const char *s) {
  setFont(*tft, Font::F2);
  return tft->textWidth(s);
}

void shortCount(uint16_t n, char *out, size_t len) {
  if (n >= 10000) snprintf(out, len, "%uk", (unsigned)(n / 1000));
  else if (n >= 1000) snprintf(out, len, "%u.%uk", (unsigned)(n / 1000), (unsigned)(n % 1000 / 100));
  else snprintf(out, len, "%u", (unsigned)n);
}

// ---- chrome ---------------------------------------------------------------------------
void drawHead(const TodaySummary &s, bool force) {
  char right[40];
  bool warn = true;
  if (!g_trafficUp) snprintf(right, sizeof(right), "Traffic offline");
  else if (!sdOk()) snprintf(right, sizeof(right), "No SD card: no log");
  else {
    warn = false;
    right[0] = '\0';
    struct tm lt;
    static const char *DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char *MONS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    if (localNow(lt)) snprintf(right, sizeof(right), "%s  %s %d", DAYS[lt.tm_wday], MONS[lt.tm_mon], lt.tm_mday);
  }
  const uint32_t h = fnv(right) ^ (uint32_t)warn;
  if (!force && h == g_sigHead) return;
  g_sigHead = h;
  TFT_eSPI &g = *tft;
  g.fillRect(120, 0, SCREEN_W - 120, 30, COL_BG);
  if (warn) {
    drawText(g, right, 296, 21, Font::F2, COL_WARN, R_BASELINE);
    g.fillCircle(305, 16, 3, COL_WARN);
  } else {
    drawText(g, right, 312, 21, Font::F2, COL_MUTED, R_BASELINE);
  }
}

// ---- stats row ------------------------------------------------------------------------
void drawStats(const TodaySummary &s, uint16_t nearby, bool force) {
  const bool synced = todayClockValid();
  char ov[8], nb[12], rare[28] = "", state[16] = "", sub[16] = "";
  if (synced) snprintf(ov, sizeof(ov), "%u", (unsigned)s.overhead);
  else snprintf(ov, sizeof(ov), "--");
  if (!synced) snprintf(nb, sizeof(nb), "--");
  else {
    char c[8];
    shortCount(nearby, c, sizeof(c));
    snprintf(nb, sizeof(nb), "%s%s", s.nearbyLost ? "~" : "", c);
  }
  if (!synced) snprintf(state, sizeof(state), "waiting");
  else if (!sdOk()) snprintf(state, sizeof(state), "needs SD");
  else if (s.historyDays < TODAY_LEARN_DAYS) {
    snprintf(state, sizeof(state), "learning");
    snprintf(sub, sizeof(sub), "day %u of %u", (unsigned)(s.historyDays + 1), (unsigned)TODAY_LEARN_DAYS);
  } else if (!s.rarest[0]) snprintf(state, sizeof(state), "none new");
  else {
    const char *mfr, *model;
    snprintf(rare, sizeof(rare), "%s", typeLookup(s.rarest, mfr, model) ? model : s.rarest);
  }
  // three cells, three signatures: a new nearby plane redraws only its own cell (S4)
  char sig[64];
  snprintf(sig, sizeof(sig), "%s|%d", nb, (int)s.nearbyLost);
  const uint32_t hOv = fnv(ov), hNb = fnv(sig);
  snprintf(sig, sizeof(sig), "%s|%s|%s", rare, state, sub);
  const uint32_t hRare = fnv(sig);

  TFT_eSPI &g = *tft;
  if (force || hOv != g_sigOv) {
    g_sigOv = hOv;
    g.fillRect(0, 32, 94, 46, COL_BG);
    drawText(g, "OVERHEAD", 10, 42, Font::Glcd, COL_DIM);
    drawNumber(g, ov, 10, 70, Font::Fsb18, synced ? COL_TEXT : COL_MUTED);
  }
  if (force || hNb != g_sigNb) {
    g_sigNb = hNb;
    g.fillRect(94, 32, 94, 48, COL_BG);
    drawText(g, "WITHIN 14 MI", 96, 42, Font::Glcd, COL_DIM);
    drawNumber(g, nb, 96, 70, Font::Fsb18, (s.nearbyLost || !synced) ? COL_MUTED : COL_TEXT);
    if (s.nearbyLost && synced) drawText(g, "since reboot", 96, 78, Font::Glcd, COL_DIM);
  }
  if (!force && hRare == g_sigRare) return;
  g_sigRare = hRare;
  g.fillRect(188, 32, SCREEN_W - 188, 48, COL_BG);
  drawText(g, "RAREST", 190, 42, Font::Glcd, COL_DIM);
  if (state[0]) {
    drawText(g, state, 190, 64, Font::Fs9, COL_MUTED);
    if (sub[0]) drawText(g, sub, 190, 76, Font::Glcd, COL_DIM);
  } else {
    static const Font fonts[] = {Font::Fsb18, Font::Fsb12, Font::Fsb9};
    Font f = Font::Fsb9;
    for (Font c : fonts) {
      setFont(g, c);
      if (numberWidth(g, rare, c) <= 124) { f = c; break; }
    }
    drawNumber(g, rare, 190, 70, f, COL_TEXT);
  }
}

// ---- hourly panel ---------------------------------------------------------------------
void drawBars(const TodaySummary &s, bool force) {
  struct tm lt;
  const int nowH = todayClockValid() && localNow(lt) ? lt.tm_hour : -1;
  char sig[128];
  int n = snprintf(sig, sizeof(sig), "%d|%lx|", nowH, (unsigned long)s.outageHours);
  for (int h = 0; h < 24 && n < (int)sizeof(sig) - 4; h++) n += snprintf(sig + n, sizeof(sig) - n, "%u,", s.hourly[h]);
  const uint32_t hb = fnv(sig);
  if (!force && hb == g_sigBars) return;
  g_sigBars = hb;

  TFT_eSPI &g = *tft;
  g.fillRoundRect(6, 80, 308, 64, 6, COL_PANEL);
  uint8_t peak = 0;
  for (int h = 0; h <= nowH && h < 24; h++) peak = max(peak, s.hourly[h]);
  char title[40];
  if (peak) snprintf(title, sizeof(title), "PASSES BY HOUR  -  PEAK %u", peak);
  else snprintf(title, sizeof(title), "PASSES BY HOUR");
  drawText(g, title, 12, 90, Font::Glcd, COL_DIM);
  const int scale = max<int>(peak, 4);
  for (int h = 0; h < 24; h++) {
    const int16_t x = BAR_X0 + h * BAR_STEP;
    const int cnt = h <= nowH ? s.hourly[h] : 0;
    if (!cnt) {
      g.drawFastHLine(x, BAR_BASE, BAR_W, COL_HAIR);
    } else {
      const int hgt = max(3, (int)lroundf((float)cnt / scale * BAR_MAX));
      g.fillRect(x, BAR_BASE - hgt + 1, BAR_W, hgt, h == nowH ? COL_TEXT : COL_MUTED);
    }
    if (s.outageHours & (1UL << h))          // not watching != quiet: the bar stays, stipple under
      for (int yy = 0; yy < 3; yy++)
        for (int xx = yy % 2; xx < BAR_W; xx += 2) g.drawPixel(x + xx, BAR_BASE + 2 + yy, COL_DIM);
    if (h == nowH) g.fillRect(x, BAR_BASE + 2, BAR_W, 2, COL_TEXT);
  }
  static const char *LAB[] = {"12A", "6A", "12P", "6P"};
  for (int i = 0; i < 4; i++) drawText(g, LAB[i], BAR_X0 + i * 6 * BAR_STEP, 141, Font::Glcd, COL_MUTED);
}

// ---- last passes ----------------------------------------------------------------------
int16_t routeW(const PassLabel &l) { return f2w(l.orig) + 4 + 14 + 4 + f2w(l.dest); }

void drawRoute(int16_t xRight, int16_t y, const PassLabel &l) {
  TFT_eSPI &g = *tft;
  int16_t x = xRight - routeW(l);
  x += drawText(g, l.orig, x, y, Font::F2, COL_MUTED) + 4;
  drawArrowRight(g, x, y - 5, 14, COL_MUTED, 4, true);
  drawText(g, l.dest, x + 18, y, Font::F2, COL_MUTED);
}

void drawRow(int16_t y, const PassRec &p, bool yday) {
  TFT_eSPI &g = *tft;
  const time_t t = p.peakEpoch;
  struct tm lt;
  localtime_r(&t, &lt);
  char hm[8], ap[4];
  fmtClock(lt, hm, sizeof(hm), ap, sizeof(ap));
  const int16_t tw = drawText(g, hm, 10, y, Font::F2, yday ? COL_DIM : COL_MUTED);
  drawText(g, ap, 10 + tw + 2, y, Font::Glcd, COL_DIM);
  const int16_t x = 10 + f2w("12:59") + 2 + 12 + 8;

  const PassLabel &l = p.lab;
  const char *right = l.reg[0] ? l.reg : l.callsign;
  const bool route = l.orig[0] && l.dest[0];
  const int16_t rw = route ? routeW(l) : f2w(right);
  char full[48], coded[32];
  if (l.op[0]) snprintf(full, sizeof(full), "%s %s", l.op, l.type);
  else snprintf(full, sizeof(full), "%s", l.type);
  if (l.op[0] && l.code[0]) snprintf(coded, sizeof(coded), "%s %s", l.code, l.type);
  else coded[0] = '\0';
  struct Cand { const char *s; bool withRight; };
  const Cand cands[] = {{full, true}, {coded, true}, {full, false}, {l.type, true}, {l.type, false}};
  const char *text = l.type;
  bool withRight = false;
  for (const Cand &c : cands) {
    if (!c.s[0]) continue;
    if (f2w(c.s) <= RIGHT_X - x - (c.withRight ? rw + GAP : 0)) {
      text = c.s;
      withRight = c.withRight;
      break;
    }
  }
  drawText(g, text, x, y, Font::F2, COL_TEXT);
  if (withRight && route) drawRoute(RIGHT_X, y, l);
  else if (withRight && right[0]) drawText(g, right, RIGHT_X, y, Font::F2, COL_MUTED, R_BASELINE);
}

void drawList(const TodaySummary &s, bool force) {
  const bool synced = todayClockValid();
  const uint32_t today = dayKeyOf(time(nullptr));
  char sig[160];
  int n = snprintf(sig, sizeof(sig), "%d|%u|", (int)synced, s.nRecent);
  for (uint8_t i = 0; i < s.nRecent && n < (int)sizeof(sig) - 24; i++)
    n += snprintf(sig + n, sizeof(sig) - n, "%s%lu%s%d,", s.recent[i].hex, (unsigned long)s.recent[i].peakEpoch,
                  s.recent[i].lab.orig, (int)(dayKeyOf(s.recent[i].peakEpoch) != today));
  const uint32_t hl = fnv(sig);
  if (!force && hl == g_sigList) return;
  g_sigList = hl;

  TFT_eSPI &g = *tft;
  g.fillRect(0, 148, SCREEN_W, SCREEN_H - 148, COL_BG);
  // Rows from an earlier day: DIM times, and the heading names how far back the list goes.
  uint32_t oldest = today;
  for (uint8_t i = 0; i < s.nRecent; i++) oldest = min(oldest, dayKeyOf(s.recent[i].peakEpoch));
  char head[40] = "LAST PASSES";
  if (oldest != today) {
    const int32_t back = spotterDaysBetween(oldest, today);
    static const char *DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    time_t o = 0;
    for (uint8_t i = 0; i < s.nRecent; i++)
      if (dayKeyOf(s.recent[i].peakEpoch) == oldest) o = s.recent[i].peakEpoch;
    struct tm ot;
    localtime_r(&o, &ot);
    if (back == 1) snprintf(head, sizeof(head), "LAST PASSES  -  SINCE YESTERDAY");
    else snprintf(head, sizeof(head), "LAST PASSES  -  SINCE %s", DAYS[ot.tm_wday]);
  }
  drawText(g, head, 10, 158, Font::Glcd, COL_DIM);
  if (!synced) {
    drawText(g, "Waiting for clock", 160, 196, Font::F2, COL_MUTED, C_BASELINE);
    return;
  }
  if (!s.nRecent) {
    drawText(g, "No overhead passes yet today", 160, 196, Font::F2, COL_MUTED, C_BASELINE);
    return;
  }
  for (uint8_t i = 0; i < s.nRecent && i < 3; i++)
    drawRow(ROW_Y[i], s.recent[i], dayKeyOf(s.recent[i].peakEpoch) != today);
}

void chevronLeft(TFT_eSPI &g, int16_t x, int16_t y, uint16_t c) {   // == map_screen.chevron_left
  g.fillTriangle(x + 7, y - 7, x + 7, y - 4, x, y, c);
  g.fillTriangle(x + 7, y - 4, x + 3, y, x, y, c);
  g.fillTriangle(x + 7, y + 7, x + 7, y + 4, x, y, c);
  g.fillTriangle(x + 7, y + 4, x + 3, y, x, y, c);
}

void drawAll(bool force) {
  uint16_t nearby;
  TodaySummary s;                             // on the loop stack: no static copy (RAM)
  todaySnapshot(s, nearby);
  drawHead(s, force);
  drawStats(s, nearby, force);
  drawBars(s, force);
  drawList(s, force);
}

}  // namespace

void todayEnter() {
  TFT_eSPI &g = *tft;
  g.fillScreen(COL_BG);
  g.fillRoundRect(4, 4, 36, 24, 12, COL_PANEL2);
  chevronLeft(g, 17, 16, COL_TEXT);
  drawText(g, "Today", 48, 23, Font::Fsb12, COL_TEXT);
  g_ver = UINT32_MAX;
  g_minute = -1;
  g_sigHead = g_sigBars = g_sigList = g_sigOv = g_sigNb = g_sigRare = 0;
  drawAll(true);
}

void todayScreenUpdate(bool trafficUp, uint32_t now) {
  (void)now;
  struct tm lt;
  const int minute = localNow(lt) ? lt.tm_hour * 60 + lt.tm_min : -2;
  if (trafficUp != g_trafficUp || todayVersion() != g_ver || minute != g_minute) {
    g_trafficUp = trafficUp;
    g_ver = todayVersion();
    g_minute = minute;
    drawAll(false);                           // each cell redraws only if its text changed
  }
}

bool todayTouchBack(int16_t x, int16_t y) { return x < 48 && y < 36; }
