// Sprite allocation, transitions, time formatting, and the boot screen.
#include "ui_internal.h"
#include "config.h"

namespace ui {
TFT_eSPI *tft = nullptr;
TFT_eSprite *dome = nullptr;
TFT_eSprite *look = nullptr;
TFT_eSprite *stats = nullptr;
TFT_eSprite *clockSpr = nullptr;
TFT_eSprite *band = nullptr;

bool localNow(struct tm &out) {
  const time_t now = time(nullptr);
  if (now < 1700000000) return false;
  localtime_r(&now, &out);
  return true;
}

void fmtClock(const struct tm &t, char *hhmm, size_t n, char *ampm, size_t m) {
#if CLOCK_24H
  snprintf(hhmm, n, "%02d:%02d", t.tm_hour, t.tm_min);
  ampm[0] = '\0';
  (void)m;
#else
  int h = t.tm_hour % 12;
  if (h == 0) h = 12;
  snprintf(hhmm, n, "%d:%02d", h, t.tm_min);
  snprintf(ampm, m, "%s", t.tm_hour < 12 ? "AM" : "PM");
#endif
}

void fmtHour(time_t t, char *out, size_t n) {
  struct tm lt;
  localtime_r(&t, &lt);
  int h = lt.tm_hour % 12;
  if (h == 0) h = 12;
  snprintf(out, n, "%d%s", h, lt.tm_hour < 12 ? "AM" : "PM");
}
}  // namespace ui

using namespace ui;

namespace {
TFT_eSprite *makeSprite(int16_t w, int16_t h, uint8_t depth) {
  auto *s = new TFT_eSprite(tft);
  s->setColorDepth(depth);
  if (!s->createSprite(w, h)) {
    Serial.printf("[ui] sprite %dx%d@%d FAILED\n", w, h, depth);
    return s;   // drawing to an unallocated sprite is a no-op; the screen degrades
  }
  if (depth == 4) s->createPalette(PALETTE16, 16);
  return s;
}
}  // namespace

void uiInit(TFT_eSPI &t) {
  tft = &t;
  // Dome first and before WiFi: it's the big contiguous block (docs/03 -> Rendering).
  dome = makeSprite(144, 144, 16);
  look = makeSprite(148, 108, 4);
  stats = makeSprite(320, 38, 4);
  clockSpr = makeSprite(120, 58, 4);
  band = makeSprite(320, 48, 4);     // plane map, drawn in 5 bands (docs/08)
  Serial.printf("[ui] sprites ready, free heap %u\n", ESP.getFreeHeap());
}

void uiWipe(bool down) {
  // 8-band wipe (~100 ms). The only deliberate blocking in the UI (06 §6).
  for (int i = 0; i < 8; i++) {
    const int band = down ? i : 7 - i;
    tft->fillRect(0, band * 30, SCREEN_W, 30, COL_BG);
    delay(12);
  }
}

// ---------------------------------------------------------------------------
//  Boot screen (screens.py -> boot)
// ---------------------------------------------------------------------------
void bootDraw(const NetStatus &n, bool full) {
  TFT_eSPI &g = *tft;
  if (full) {
    g.fillScreen(COL_BG);
    setFont(g, Font::Fsb18);
    const int16_t w = g.textWidth("SkyDesk");
    const int16_t x = 160 - (w + 30) / 2;
    drawPlaneGlyph(g, x + 11, 45, 45, 1.2f, COL_PLANE);
    drawText(g, "SkyDesk", x + 30, 56, Font::Fsb18, COL_TEXT);
    drawText(g, "v" FW_VERSION, 160, 76, Font::Glcd, COL_DIM, C_BASELINE);
  }
  g.fillRoundRect(30, 90, 260, 104, 8, COL_PANEL);
  g.fillRect(0, 198, SCREEN_W, 42, COL_BG);

  enum St { OK, FAIL, BUSY, TODO };
  auto row = [&](int16_t y, St st, const char *label, const char *detail) {
    switch (st) {
      case OK:   drawCheck(g, 46, y - 9, COL_OK); break;
      case FAIL: drawCross(g, 46, y - 9, COL_ERR); break;
      case BUSY: for (int i = 0; i < 3; i++) g.fillCircle(47 + i * 4, y - 4, 1, COL_PLANE); break;
      case TODO: g.drawCircle(50, y - 5, 4, COL_DIM); break;
    }
    drawText(g, label, 64, y, Font::F2, st == TODO ? COL_DIM : COL_TEXT);
    if (detail && detail[0]) drawText(g, detail, 280, y, Font::F2, COL_MUTED, R_BASELINE);
  };

  char buf[48];
  if (n.wifi == WifiPhase::Failed) {
    snprintf(buf, sizeof(buf), "Can't join \"%s\"", n.ssid);
    row(110, FAIL, buf, "");
    const int32_t left = (int32_t)(n.nextRetryMs - millis()) / 1000;
    snprintf(buf, sizeof(buf), "Retrying in %d s  (attempt %d)", (int)max<int32_t>(left, 0), n.attempt);
    drawText(g, buf, 64, 128, Font::F2, COL_MUTED);
    row(150, TODO, "Clock", "");
    row(172, TODO, "Weather, radar", "");
    drawText(g, "Check WIFI_SSID / WIFI_PASSWORD in secrets.h", 160, 214, Font::F2, COL_MUTED, C_BASELINE);
    drawText(g, "2.4 GHz networks only", 160, 232, Font::F2, COL_DIM, C_BASELINE);
    return;
  }
  const bool up = n.wifi == WifiPhase::Connected;
  if (up) snprintf(buf, sizeof(buf), "%.16s  %d dBm", n.ssid, n.rssi);
  row(110, up ? OK : BUSY, "WiFi", up ? buf : n.ssid);
  struct tm lt;
  char hm[8] = "", ap[4] = "";
  if (n.timeSynced && localNow(lt)) {
    fmtClock(lt, hm, sizeof(hm), ap, sizeof(ap));
    snprintf(buf, sizeof(buf), "%s %s", hm, ap);
  }
  row(132, n.timeSynced ? OK : (up ? BUSY : TODO), "Clock", n.timeSynced ? buf : "");
  row(154, n.weatherTried ? OK : (up ? BUSY : TODO), "Weather", "");
  row(176, n.radarTried ? OK : (up ? BUSY : TODO), "Radar", "");
  snprintf(buf, sizeof(buf), "%s  %.2f, %.2f", OBS_PLACE, (double)OBS_LAT, (double)OBS_LON);
  drawText(g, buf, 160, 214, Font::F2, COL_MUTED, C_BASELINE);
}
