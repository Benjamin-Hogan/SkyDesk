// ui_internal.h - state shared between the ui_*.cpp files only.
#pragma once

#include "ui.h"

namespace ui {
extern TFT_eSPI *tft;
extern TFT_eSprite *dome;     // 144x144, 16-bit (anti-aliased primitives OK)
extern TFT_eSprite *look;     // LOOK block, 4-bit, PALETTE16 indices
extern TFT_eSprite *stats;    // plane stats bar, 4-bit
extern TFT_eSprite *band;       // plane map band, 320x48 4-bit, MAP_PALETTE (docs/08)
extern TFT_eSprite *clockSpr;   // weather clock block, 4-bit

// Local time helpers (return false until NTP has synced).
bool localNow(struct tm &out);
void fmtClock(const struct tm &t, char *hhmm, size_t n, char *ampm, size_t m);
void fmtHour(time_t t, char *out, size_t n);   // "4PM"
}  // namespace ui
