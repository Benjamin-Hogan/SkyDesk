// Host tests for touch_map.h: raw -> flip -> screen, and calibration in both orientations
// (Flip screen, Mr Stacks round 1 M2: recovery must be designed, not assumed).
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

#include "touch_map.h"

extern int g_fail;
#define CHECK(cond, msg)                                                             \
  do {                                                                               \
    if (!(cond)) { std::printf("  FAIL: %s (line %d)\n", msg, __LINE__); g_fail++; } \
    else std::printf("  ok:   %s\n", msg);                                          \
  } while (0)

namespace {
// A fake panel: raw = 200 + 11 * base x, 240 + 14.6 * base y (the CYD's rough scale).
void panel(int bx, int by, int16_t r[2]) { r[0] = (int16_t)(200 + 11 * bx); r[1] = (int16_t)(240 + 146 * by / 10); }
bool near(int a, int b) { return std::abs(a - b) <= 2; }
}  // namespace

void testTouchMap() {
  std::printf("touch: raw -> flip -> screen, calibration either way up\n");
  const int16_t W = 320, H = 240;
  const TouchCal def{200, (int16_t)(200 + 11 * 319), 240, (int16_t)(240 + 146 * 239 / 10)};
  int16_t r[2], x, y;
  panel(20, 30, r);                                     // a press at BASE (20,30)
  touchMapPoint(r[0], r[1], def, false, W, H, x, y);
  CHECK(near(x, 20) && near(y, 30), "normal: the base point");
  touchMapPoint(r[0], r[1], def, true, W, H, x, y);
  CHECK(near(x, 299) && near(y, 209), "flipped: the same press lands mirrored on screen");

  for (bool flip : {false, true}) {
    const int16_t p0[2] = {20, 20}, p1[2] = {300, 220};  // crosses drawn the CURRENT way up
    int16_t r0[2], r1[2];
    panel(flip ? W - 1 - p0[0] : p0[0], flip ? H - 1 - p0[1] : p0[1], r0);   // where the finger really is
    panel(flip ? W - 1 - p1[0] : p1[0], flip ? H - 1 - p1[1] : p1[1], r1);
    const TouchCal c = touchCalibrate(p0, p1, r0, r1, flip, W, H);
    CHECK(near(c.xMin, def.xMin) && near(c.xMax, def.xMax) && near(c.yMin, def.yMin) && near(c.yMax, def.yMax),
          flip ? "calibrated while flipped: BASE extents" : "calibrated normal: BASE extents");
    int16_t q[2];
    panel(flip ? W - 1 - 160 : 160, flip ? H - 1 - 100 : 100, q);          // tap the screen point (160,100)
    touchMapPoint(q[0], q[1], c, flip, W, H, x, y);
    CHECK(near(x, 160) && near(y, 100), flip ? "flipped cal: a tap lands where drawn" : "normal cal: a tap lands where drawn");
  }
}
