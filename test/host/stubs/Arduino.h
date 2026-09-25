// Minimal Arduino shim so pure-logic modules (geo, tracker) build on the host.
#pragma once
#define _USE_MATH_DEFINES
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

using std::max;
using std::min;

#ifndef PI
#define PI 3.14159265358979323846
#endif
#define DEG_TO_RAD 0.017453292519943295
#define RAD_TO_DEG 57.29577951308232

struct HostSerial {
  bool quiet = false;
  template <class... A> void printf(const char *f, A... a) { if (!quiet) ::printf(f, a...); }
  void println(const char *s) { if (!quiet) ::puts(s); }
};
extern HostSerial Serial;
