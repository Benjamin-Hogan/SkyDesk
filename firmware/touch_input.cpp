#include "touch_input.h"
#include "config.h"
#include "settings.h"

#include <SPI.h>
#include <XPT2046_Touchscreen.h>

namespace {
SPIClass g_spi(HSPI);
// Constructed WITHOUT the IRQ pin on purpose: IRQ mode silently fails on many
// CYD units (docs/02-hardware.md). Polling over SPI is reliable.
XPT2046_Touchscreen g_ts(TOUCH_CS);

bool     g_down = false;
uint32_t g_downMs = 0;
int16_t  g_x = 0, g_y = 0;
bool     g_holdFired = false;
uint8_t  g_upCount = 0;       // debounce release

int16_t mapAxis(int raw, int rmin, int rmax, int range) {
  if (rmin == rmax) return 0;
  long v = map(raw, rmin, rmax, 0, range - 1);
  return (int16_t)constrain(v, 0, range - 1);
}
}  // namespace

void touchInit() {
  g_spi.begin(TOUCH_SCLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  g_ts.begin(g_spi);
  g_ts.setRotation(TFT_ROTATION);
}

bool touchRaw(int16_t &rx, int16_t &ry) {
  if (!g_ts.touched()) return false;
  TS_Point p = g_ts.getPoint();
  if (p.z < TOUCH_PRESSURE_MIN) return false;
  rx = p.x;
  ry = p.y;
  return true;
}

TouchEvent touchPoll(uint32_t now) {
  TouchEvent e{TouchEvt::None, g_x, g_y};
  int16_t rx, ry;
  const bool pressed = touchRaw(rx, ry);

  if (pressed) {
    g_upCount = 0;
    if (!g_down) {
      const Settings &s = settings();
      g_down = true;
      g_downMs = now;
      g_holdFired = false;
      g_x = mapAxis(rx, s.tXMin, s.tXMax, SCREEN_W);
      g_y = mapAxis(ry, s.tYMin, s.tYMax, SCREEN_H);
      e.x = g_x;
      e.y = g_y;
    }
    // Hold fires WHILE held (settings); nothing else fires during a press.
    if (!g_holdFired && now - g_downMs >= SETTINGS_HOLD_MS) {
      g_holdFired = true;
      e.evt = TouchEvt::Hold;
    }
    return e;
  }

  if (g_down && ++g_upCount >= 3) {   // ~3 polls without contact = released
    g_down = false;
    const uint32_t held = now - g_downMs;
    // Precedence (docs/06-ui-spec.md §7): after Hold, the release does nothing.
    if (g_holdFired) return e;
    if (held <= TAP_MAX_MS) e.evt = TouchEvt::Tap;
    else if (held >= LONG_PRESS_MS) e.evt = TouchEvt::LongPress;
  }
  return e;
}
