// settings.h - user settings persisted in NVS (Preferences namespace "skydesk").
#pragma once

#include <Arduino.h>

struct Settings {
  int16_t viewUpDeg;     // Setup -> Facing. 0 = north-up dome
  bool    nightDim;      // dim backlight at night
  uint8_t mapZoom;       // plane map zoom index (0 = 5 mi, 1 = 10 mi, 2 = 20 mi)
  bool    touchCal;      // calibration below is valid
  int16_t tXMin, tXMax, tYMin, tYMax;  // raw XPT2046 values at screen edges
};

void settingsLoad();
void settingsSave();
Settings &settings();
