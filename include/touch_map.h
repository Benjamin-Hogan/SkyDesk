// touch_map.h - pure touch geometry: raw (panel-native, BASE orientation) -> screen, with the
// Settings -> Flip screen mirror applied last; and the two-cross calibration that stores its
// extents in the BASE frame whichever way up it was run. Host-tested (test_touchmap.cpp).
#pragma once

#include <stdint.h>

struct TouchCal { int16_t xMin, xMax, yMin, yMax; };   // raw values at the screen edges (BASE frame)

inline int16_t touchMapAxis(int raw, int rmin, int rmax, int range) {
  if (rmin == rmax) return 0;
  long v = (long)(raw - rmin) * (range - 1) / (rmax - rmin);
  return (int16_t)(v < 0 ? 0 : v > range - 1 ? range - 1 : v);
}

inline void touchMapPoint(int rx, int ry, const TouchCal &c, bool flip, int16_t w, int16_t h, int16_t &x, int16_t &y) {
  x = touchMapAxis(rx, c.xMin, c.xMax, w);
  y = touchMapAxis(ry, c.yMin, c.yMax, h);
  if (flip) { x = w - 1 - x; y = h - 1 - y; }
}

// Two crosses drawn at SCREEN points p0, p1 (the current way up) read raw r0, r1. The extents are
// solved in the BASE frame: a flipped screen point is mirrored back first.
inline TouchCal touchCalibrate(const int16_t p0[2], const int16_t p1[2], const int16_t r0[2], const int16_t r1[2],
                               bool flip, int16_t w, int16_t h) {
  const int16_t b0x = flip ? w - 1 - p0[0] : p0[0], b0y = flip ? h - 1 - p0[1] : p0[1];
  const int16_t b1x = flip ? w - 1 - p1[0] : p1[0], b1y = flip ? h - 1 - p1[1] : p1[1];
  const float kx = (r1[0] - r0[0]) / float(b1x - b0x), ky = (r1[1] - r0[1]) / float(b1y - b0y);
  TouchCal c;
  c.xMin = (int16_t)(r0[0] - kx * b0x);
  c.xMax = (int16_t)(c.xMin + kx * (w - 1));
  c.yMin = (int16_t)(r0[1] - ky * b0y);
  c.yMax = (int16_t)(c.yMin + ky * (h - 1));
  return c;
}
