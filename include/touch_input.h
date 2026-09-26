// touch_input.h - XPT2046 polling -> tap / long-press / hold events.
#pragma once

#include <Arduino.h>

enum class TouchEvt : uint8_t {
  None,
  Tap,        // released within TAP_MAX_MS
  LongPress,  // released after LONG_PRESS_MS..SETTINGS_HOLD_MS
  Hold,       // held SETTINGS_HOLD_MS (fires once, while still held)
};

struct TouchEvent {
  TouchEvt evt;
  int16_t x, y;   // screen coords at touch-down
};

void touchInit();
TouchEvent touchPoll(uint32_t nowMs);

// The current press in screen coordinates (calibrated), false if not pressed. For the
// setup portal's hold-to-cancel fill (docs/12), which needs the live state, not events.
bool touchPoint(int16_t &x, int16_t &y);

// Raw 12-bit reading for calibration. False if not pressed.
bool touchRaw(int16_t &rx, int16_t &ry);
