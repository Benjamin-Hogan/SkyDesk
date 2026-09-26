// Settings: menu, Facing dial (screens.py -> setup_facing), touch calibration.
// Entered by holding the screen 3 s anywhere. Holding 3 s again inside
// Settings jumps straight to touch calibration (works even with bad touch mapping).
#include "geo.h"
#include "settings.h"
#include "touch_input.h"
#include "touch_map.h"
#include "ui_internal.h"

using namespace ui;

namespace {

enum class Page : uint8_t { Menu, Facing, Cal };
Page g_page = Page::Menu;
uint8_t g_calStep = 0;
int16_t g_calRaw[2][2];
bool g_calWasDown = false;
uint32_t g_quietUntil = 0;   // Flip screen: touch ignored until a clean release + 400 ms (docs/12)
bool g_quiet = false;

struct Btn { int16_t x, y, w, h; };
bool hit(const Btn &b, int16_t x, int16_t y) { return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h; }

// 5 rows x 36 px at y 34 + 40 i and the back pill (docs/12 "Flip screen";
// screens: portal_screen.settings_menu). For touch each row owns its full 40 px band and the
// back zone is x < 48, y < 34. Row 1's band starts at 34 and the last one runs to the bottom edge.
enum MenuRow : uint8_t { ROW_FACING, ROW_NIGHT, ROW_FLIP, ROW_PHONE, ROW_CAL, ROW_COUNT };
constexpr int16_t MENU_Y0 = 34, MENU_PITCH = 40;
const Btn MENU_BACK{0, 0, 48, 34};
int menuRowAt(int16_t x, int16_t y) {
  if (x < 6 || x >= 314 || y < MENU_Y0) return -1;
  const int i = (y - (MENU_Y0 - 2)) / MENU_PITCH;
  return i < ROW_COUNT ? i : ROW_COUNT - 1;
}
const Btn FACE_MINUS{14, 104, 56, 48}, FACE_PLUS{250, 104, 56, 48}, FACE_DONE{110, 202, 100, 30};

void doneButton(const Btn &b) {
  tft->fillRoundRect(b.x, b.y, b.w, b.h, b.h / 2, COL_PLANE);
  drawText(*tft, "Done", b.x + b.w / 2, b.y + b.h / 2 + 6, Font::Fsb9, COL_BG, C_BASELINE);
}

void menuRow(int i, const char *label, const char *value) {
  TFT_eSPI &g = *tft;
  const Btn b{14, (int16_t)(MENU_Y0 + i * MENU_PITCH), 292, 36};
  g.fillRoundRect(b.x, b.y, b.w, b.h, 8, COL_PANEL2);
  drawText(g, label, b.x + 12, b.y + 24, Font::Fs9, COL_TEXT);
  drawText(g, value, b.x + b.w - 24, b.y + 23, Font::F2, COL_MUTED, R_BASELINE);
  drawChevron(g, b.x + b.w - 16, b.y + b.h / 2, COL_MUTED);
}

void drawMenu() {
  TFT_eSPI &g = *tft;
  g.fillScreen(COL_BG);
  g.fillRoundRect(4, 4, 36, 24, 12, COL_PANEL2);
  g.fillTriangle(24, 9, 24, 12, 17, 16, COL_TEXT);   // the radar / Today back chevron
  g.fillTriangle(24, 12, 20, 16, 17, 16, COL_TEXT);
  g.fillTriangle(24, 23, 24, 20, 17, 16, COL_TEXT);
  g.fillTriangle(24, 20, 20, 16, 17, 16, COL_TEXT);
  drawText(g, "Settings", 48, 23, Font::Fsb12, COL_TEXT);
  char buf[16];
  const Settings &s = settings();
  if (s.viewUpDeg == 0) snprintf(buf, sizeof(buf), "north-up");
  else snprintf(buf, sizeof(buf), "%d` %s", s.viewUpDeg, geo::compass16(s.viewUpDeg));   // ` = degree in Font 2
  menuRow(ROW_FACING, "Facing direction", buf);
  menuRow(ROW_NIGHT, "Dim at night", s.nightDim ? "On" : "Off");
  menuRow(ROW_FLIP, "Flip screen", s.flip ? "Turned" : "Normal");
  menuRow(ROW_PHONE, "Phone setup", "WiFi, location");
  menuRow(ROW_CAL, "Calibrate touch", s.touchCal ? "done" : "not set");   // (the 3 s hold also gets here)
}

void drawFacing() {
  TFT_eSPI &g = *tft;
  const int16_t vu = settings().viewUpDeg;
  g.fillScreen(COL_BG);
  drawText(g, "Which way do you face?", 160, 22, Font::Fsb9, COL_TEXT, C_BASELINE);
  drawText(g, "Stand where you'd watch SkyDesk.", 160, 40, Font::F2, COL_MUTED, C_BASELINE);
  const int16_t cx = 160, cy = 132, r = 58;
  g.fillCircle(cx, cy, r, COL_PANEL);
  g.drawCircle(cx, cy, r, COL_HAIR);
  const char *labs[] = {"N", "E", "S", "W"};
  for (int i = 0; i < 4; i++) {
    const float th = (i * 90 - vu) * DEG_TO_RAD;
    drawText(g, labs[i], lroundf(cx + (r - 12) * sinf(th)), lroundf(cy - (r - 12) * cosf(th)) + 5, Font::Fsb9,
             i == 0 ? COL_PLANE : COL_MUTED, C_BASELINE);
  }
  g.fillTriangle(cx - 9, cy - r - 10, cx + 9, cy - r - 10, cx, cy - r + 2, COL_TEXT);
  drawText(g, "YOU FACE", cx + 14, cy - r - 2, Font::Glcd, COL_MUTED);
  drawDegrees(g, vu, cx, cy + 7, Font::Fsb12, COL_TEXT, C_BASELINE);
  drawText(g, geo::compass16(vu), cx, cy + 24, Font::F2, COL_MUTED, C_BASELINE);
  for (const Btn *b : {&FACE_MINUS, &FACE_PLUS}) {
    g.fillRoundRect(b->x, b->y, b->w, b->h, 8, COL_PANEL2);
    drawText(g, b == &FACE_MINUS ? "-15" : "+15", b->x + b->w / 2, b->y + 30, Font::Fsb9, COL_TEXT, C_BASELINE);
  }
  drawText(g, "Use a phone", 42, 172, Font::Glcd, COL_DIM, C_BASELINE);
  drawText(g, "compass", 42, 182, Font::Glcd, COL_DIM, C_BASELINE);
  doneButton(FACE_DONE);
}

const int16_t CAL_PTS[2][2] = {{20, 20}, {300, 220}};

void drawCal() {
  TFT_eSPI &g = *tft;
  g.fillScreen(COL_BG);
  drawText(g, "Touch calibration", 160, 110, Font::Fsb9, COL_TEXT, C_BASELINE);
  drawText(g, "Tap the centre of the cross", 160, 132, Font::F2, COL_MUTED, C_BASELINE);
  const int16_t x = CAL_PTS[g_calStep][0], y = CAL_PTS[g_calStep][1];
  g.drawFastHLine(x - 10, y, 21, COL_PLANE);
  g.drawFastVLine(x, y - 10, 21, COL_PLANE);
  g.drawCircle(x, y, 5, COL_PLANE);
}

void finishCal() {
  // Extrapolate the raw readings at the two crosses out to the screen edges.
  Settings &s = settings();
  // The crosses were drawn the CURRENT way up; the extents are stored in the BASE frame
  // (touch_map.h, host-tested both ways).
  const TouchCal c = touchCalibrate(CAL_PTS[0], CAL_PTS[1], g_calRaw[0], g_calRaw[1], s.flip, SCREEN_W, SCREEN_H);
  s.tXMin = c.xMin;
  s.tXMax = c.xMax;
  s.tYMin = c.yMin;
  s.tYMax = c.yMax;
  s.touchCal = true;
  settingsSave();
  Serial.printf("[touch] cal x %d..%d  y %d..%d\n", s.tXMin, s.tXMax, s.tYMin, s.tYMax);
}

}  // namespace

void setupEnter() {
  g_page = Page::Menu;
  g_quiet = false;
  drawMenu();
}

void setupStartCal() {
  g_page = Page::Cal;
  g_calStep = 0;
  g_calWasDown = true;   // ignore the press that got us here
  drawCal();
}

SetupResult setupTouch(int16_t x, int16_t y) {
  Settings &s = settings();
  switch (g_page) {
    case Page::Menu:
      if (g_quiet) break;   // just flipped: this is the bounce
      if (hit(MENU_BACK, x, y)) return SetupResult::Done;
      switch (menuRowAt(x, y)) {
        case ROW_FACING: g_page = Page::Facing; drawFacing(); break;
        case ROW_NIGHT: s.nightDim = !s.nightDim; settingsSave(); drawMenu(); break;
        case ROW_FLIP:   // a Tap is already the release (touch_input.cpp)
          s.flip = !s.flip;
          settingsSave();
          tft->setRotation(screenRotation());
          drawMenu();
          g_quietUntil = millis() + 400;
          g_quiet = true;
          break;
        case ROW_PHONE: return SetupResult::Portal;   // main reboots into the portal
        case ROW_CAL: setupStartCal(); break;
        default: break;
      }
      break;
    case Page::Facing:
      if (hit(FACE_MINUS, x, y)) { s.viewUpDeg = (s.viewUpDeg + 345) % 360; drawFacing(); }
      else if (hit(FACE_PLUS, x, y)) { s.viewUpDeg = (s.viewUpDeg + 15) % 360; drawFacing(); }
      else if (hit(FACE_DONE, x, y)) { settingsSave(); g_page = Page::Menu; drawMenu(); }
      break;
    case Page::Cal:
      break;   // handled by setupTick with raw readings
  }
  return SetupResult::None;
}

void setupTick(uint32_t now) {
  if (g_quiet) {   // after a flip: any press restarts the 400 ms
    int16_t x, y;
    if (touchPoint(x, y)) g_quietUntil = now + 400;
    else if ((int32_t)(now - g_quietUntil) >= 0) g_quiet = false;
  }
  if (g_page != Page::Cal) return;
  int16_t rx, ry;
  const bool down = touchRaw(rx, ry);
  if (down && !g_calWasDown) {
    g_calRaw[g_calStep][0] = rx;
    g_calRaw[g_calStep][1] = ry;
    if (++g_calStep >= 2) {
      finishCal();
      g_page = Page::Menu;
      drawMenu();
    } else {
      drawCal();
    }
  }
  g_calWasDown = down;
}

bool setupInCalibration() { return g_page == Page::Cal; }
