#include "settings.h"
#include "config.h"

#include <Preferences.h>

namespace {
Settings g_s;
}

Settings &settings() { return g_s; }

void settingsLoad() {
  Preferences p;
  p.begin("skydesk", true);
  g_s.viewUpDeg = p.getShort("viewUp", VIEW_UP_DEG_DEFAULT);
  g_s.nightDim = p.getBool("nightDim", true);
  g_s.touchCal = p.getBool("tcal", false);
  g_s.tXMin = p.getShort("txmin", TOUCH_RAW_MIN_X);
  g_s.tXMax = p.getShort("txmax", TOUCH_RAW_MAX_X);
  g_s.tYMin = p.getShort("tymin", TOUCH_RAW_MIN_Y);
  g_s.tYMax = p.getShort("tymax", TOUCH_RAW_MAX_Y);
  g_s.mapZoom = p.getUChar("mapZoom", MAP_DEFAULT_ZOOM);
  if (g_s.mapZoom > 2) g_s.mapZoom = MAP_DEFAULT_ZOOM;
  p.end();
  Serial.printf("[cfg] viewUp=%d nightDim=%d touchCal=%d\n", g_s.viewUpDeg, g_s.nightDim, g_s.touchCal);
}

void settingsSave() {
  Preferences p;
  p.begin("skydesk", false);
  p.putShort("viewUp", g_s.viewUpDeg);
  p.putBool("nightDim", g_s.nightDim);
  p.putBool("tcal", g_s.touchCal);
  p.putShort("txmin", g_s.tXMin);
  p.putShort("txmax", g_s.tXMax);
  p.putShort("tymin", g_s.tYMin);
  p.putShort("tymax", g_s.tYMax);
  p.putUChar("mapZoom", g_s.mapZoom);
  p.end();
}
