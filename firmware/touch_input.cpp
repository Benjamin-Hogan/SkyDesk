#include "touch_input.h"
#include "config.h"
#include "settings.h"

// XPT2046, BIT-BANGED (v3, docs/10 -> Hardware). The hardware HSPI peripheral now
// belongs to the microSD card; the TFT keeps VSPI. The controller is slow and only
// polled every UI tick, so software SPI costs nothing. The sampling sequence, the
// "best two of three" averaging, the Z threshold and the rotation are copied from
// XPT2046_Touchscreen v1.4, so touch calibrations saved in NVS stay valid.
// No IRQ pin on purpose: IRQ mode silently fails on many CYD units (docs/02).

namespace {

constexpr int Z_THRESHOLD = 400;

bool     g_down = false;
uint32_t g_downMs = 0;
int16_t  g_x = 0, g_y = 0;
bool     g_holdFired = false;
uint8_t  g_upCount = 0;       // debounce release

// SPI mode 0, MSB first, ~1 MHz: MOSI set while SCLK is low, MISO read after the rising edge.
uint16_t xfer16(uint16_t out) {
  uint16_t in = 0;
  for (int i = 15; i >= 0; i--) {
    digitalWrite(TOUCH_MOSI, (out >> i) & 1);
    delayMicroseconds(1);
    digitalWrite(TOUCH_SCLK, HIGH);
    delayMicroseconds(1);
    in = (in << 1) | (digitalRead(TOUCH_MISO) & 1);
    digitalWrite(TOUCH_SCLK, LOW);
  }
  return in;
}

// A real 8-clock transfer: the library sends the Z1 command with an 8-bit transfer().
// (Clocking 16 here misaligned the Z1 result, so pressure was noise - reviewer H1.)
uint8_t xfer8(uint8_t out) {
  uint8_t in = 0;
  for (int i = 7; i >= 0; i--) {
    digitalWrite(TOUCH_MOSI, (out >> i) & 1);
    delayMicroseconds(1);
    digitalWrite(TOUCH_SCLK, HIGH);
    delayMicroseconds(1);
    in = (uint8_t)((in << 1) | (digitalRead(TOUCH_MISO) & 1));
    digitalWrite(TOUCH_SCLK, LOW);
  }
  return in;
}

int16_t bestTwoAvg(int16_t x, int16_t y, int16_t z) {
  const int16_t da = abs(x - y), db = abs(x - z), dc = abs(z - y);
  if (da <= db && da <= dc) return (x + y) >> 1;
  if (db <= da && db <= dc) return (x + z) >> 1;
  return (y + z) >> 1;
}

// One reading: pressure z plus raw x/y after rotation (0..4095). Mirrors
// XPT2046_Touchscreen::update() command for command.
bool sample(int16_t &xr, int16_t &yr, int &zOut) {
  int16_t data[6] = {0};
  digitalWrite(TOUCH_CS, LOW);
  // The library sends 0xB1 as a lone byte, then 16-bit transfers; the controller
  // clocks a conversion out on the 16 clocks after each command byte.
  xfer8(0xB1);                                   // Z1
  const int16_t z1 = xfer16(0xC1) >> 3;          // Z2
  int z = z1 + 4095;
  const int16_t z2 = xfer16(0x91) >> 3;          // X
  z -= z2;
  if (z >= Z_THRESHOLD) {
    xfer16(0x91);                                // dummy X, the first is always noisy
    data[0] = xfer16(0xD1) >> 3;
    data[1] = xfer16(0x91) >> 3;
    data[2] = xfer16(0xD1) >> 3;
    data[3] = xfer16(0x91) >> 3;
  }
  data[4] = xfer16(0xD0) >> 3;                   // last Y, power down
  data[5] = xfer16(0) >> 3;
  digitalWrite(TOUCH_CS, HIGH);
  zOut = z < 0 ? 0 : z;
  if (z < Z_THRESHOLD) return false;
  const int16_t x = bestTwoAvg(data[0], data[2], data[4]);
  const int16_t y = bestTwoAvg(data[1], data[3], data[5]);
  switch (TFT_ROTATION & 3) {
    case 0: xr = 4095 - y; yr = x; break;
    case 1: xr = x; yr = y; break;
    case 2: xr = y; yr = 4095 - x; break;
    default: xr = 4095 - x; yr = 4095 - y; break;
  }
  return true;
}

int16_t mapAxis(int raw, int rmin, int rmax, int range) {
  if (rmin == rmax) return 0;
  long v = map(raw, rmin, rmax, 0, range - 1);
  return (int16_t)constrain(v, 0, range - 1);
}
}  // namespace

void touchInit() {
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);
  pinMode(TOUCH_SCLK, OUTPUT);
  digitalWrite(TOUCH_SCLK, LOW);
  pinMode(TOUCH_MOSI, OUTPUT);
  pinMode(TOUCH_MISO, INPUT);
}

bool touchRaw(int16_t &rx, int16_t &ry) {
  int z;
  if (!sample(rx, ry, z)) return false;
  return z >= TOUCH_PRESSURE_MIN;
}

bool touchPoint(int16_t &x, int16_t &y) {
  int16_t rx, ry;
  if (!touchRaw(rx, ry)) return false;
  const Settings &s = settings();
  x = mapAxis(rx, s.tXMin, s.tXMax, SCREEN_W);
  y = mapAxis(ry, s.tYMin, s.tYMax, SCREEN_H);
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
