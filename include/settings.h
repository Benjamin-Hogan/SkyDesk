// settings.h - user settings persisted in NVS (Preferences namespace "skydesk").
#pragma once

#include <Arduino.h>

#include "setup_model.h"   // PortalCfg (pure, so the portal's save rules are host-tested)

struct Settings {
  int16_t viewUpDeg;     // Setup -> Facing. 0 = north-up dome
  bool    nightDim;      // dim backlight at night
  uint8_t mapZoom;       // plane map zoom index (0 = 5 mi, 1 = 10 mi, 2 = 20 mi)
  bool    touchCal;      // calibration below is valid (in the BASE orientation)
  bool    flip;          // Settings -> Flip screen: the display turned 180 degrees
  int16_t tXMin, tXMax, tYMin, tYMax;  // raw XPT2046 values at screen edges
  PortalCfg net;         // WiFi, location, time zone (portal)
  char apPw[9];          // the portal hotspot password, generated once per device
};

void settingsLoad();
void settingsSave();                       // the small settings (facing, dim, zoom, touch)
void settingsSavePortal();                 // the PortalCfg blob, one NVS write
Settings &settings();
uint8_t screenRotation();                  // TFT rotation in effect: 1, or 3 when flipped
const char *settingsTzPosix();             // the POSIX TZ string in effect

// The setup boot (docs/12 -> Modes): request it (then reboot), and take it at boot. Taking
// CLEARS the flag first, so a crash or power cut in setup mode comes back in normal mode.
void settingsRequestSetup(bool automatic);
bool settingsTakeSetupRequest(bool &automatic);
