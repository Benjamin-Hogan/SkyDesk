// ui.h - screens and drawing helpers. Layout source of truth:
// docs/mockups/screens.py (drawn with the same fonts); rules: docs/06-ui-spec.md.
#pragma once

#include <TFT_eSPI.h>

#include "app_state.h"
#include "route_client.h"
#include "tracker.h"
#include "weather_client.h"

// ---------------------------------------------------------------------------
//  Typography tokens (06 §1)
// ---------------------------------------------------------------------------
enum class Font : uint8_t { Glcd, F2, Fs9, Fsb9, Fsb12, Fsb18, Fsb24 };
void setFont(TFT_eSPI &g, Font f);
int16_t capHeight(Font f);

// 16-entry palette shared by all 4-bit text sprites (COL_MOON is never needed there).
enum Pal : uint8_t {
  P_BG, P_PANEL, P_PANEL2, P_HAIR, P_TEXT, P_MUTED, P_DIM, P_PLANE, P_PLANE_DIM,
  P_PLANE_FAINT, P_OK, P_WARN, P_ERR, P_RAIN, P_CLOUD, P_SUN
};
extern const uint16_t PALETTE16[16];

// ---------------------------------------------------------------------------
//  Helpers (06 §1 "Glyphs that do NOT exist"). `c` is RGB565 on the TFT or a
//  16-bit sprite, or a Pal index on a 4-bit sprite.
// ---------------------------------------------------------------------------
// Draw text on the baseline. datum: L_BASELINE / C_BASELINE / R_BASELINE.
int16_t drawText(TFT_eSPI &g, const char *s, int16_t x, int16_t y, Font f, uint16_t c,
                 uint8_t datum = L_BASELINE);
// Numbers: +1 px tracking for fsb9 (its '8' has zero side-bearing).
int16_t drawNumber(TFT_eSPI &g, const char *s, int16_t x, int16_t y, Font f, uint16_t c,
                   uint8_t datum = L_BASELINE);
int16_t numberWidth(TFT_eSPI &g, const char *s, Font f);
// Temperature/angle: number + degree ring. Returns total width.
int16_t drawDegrees(TFT_eSPI &g, int value, int16_t x, int16_t y, Font f, uint16_t c,
                    uint8_t datum = L_BASELINE);
void drawDegreeRing(TFT_eSPI &g, int16_t x, int16_t yBase, Font f, uint16_t c);
void drawArrowRight(TFT_eSPI &g, int16_t x, int16_t yMid, int16_t len, uint16_t c,
                    int16_t head = 5, bool thin = false);
void drawSep(TFT_eSPI &g, int16_t x, int16_t y, uint16_t c, int16_t r = 1);
void drawCheck(TFT_eSPI &g, int16_t x, int16_t y, uint16_t c);
void drawCross(TFT_eSPI &g, int16_t x, int16_t y, uint16_t c);
void drawChevron(TFT_eSPI &g, int16_t x, int16_t yMid, uint16_t c);
// Fit text into maxW: first font that fits from `fonts`, else truncate + dots.
int16_t drawFit(TFT_eSPI &g, const char *s, int16_t x, int16_t y, int16_t maxW, uint16_t c,
                const Font *fonts, uint8_t nFonts);
// Aircraft glyph (6 convex parts). headingDeg: 0 = screen up.
void drawPlaneGlyph(TFT_eSPI &g, float cx, float cy, float headingDeg, float scale, uint16_t c);
// Weather icon at centre (cx,cy), scale s (1.0 ~ 34 px). bg = container colour.
void drawWxIcon(TFT_eSPI &g, int16_t cx, int16_t cy, float s, WxKind k, bool day, uint16_t bg);

// ---------------------------------------------------------------------------
//  Screens
// ---------------------------------------------------------------------------
void uiInit(TFT_eSPI &tft);          // allocates sprites - call before WiFi
void uiWipe(bool down);              // 8-band transition

void bootDraw(const NetStatus &n, bool full);

void weatherEnter();
void weatherUpdate(const Weather &w, const Traffic &t, bool trafficUp, uint32_t now);

void planeEnter();
void planeUpdate(const TrackView &v, const Traffic &t, uint32_t now, bool arrival);

// Plane map (docs/08-plane-map.md)
enum class MapAction : uint8_t { None, Back, OpenPlane };
void mapEnter();
void mapLeave();
void mapUpdate(const Traffic &t, bool trafficUp, uint32_t now);
MapAction mapTouch(const Traffic &t, int16_t x, int16_t y, char *hexOut);   // hexOut: 7 bytes
void mapCardClosed(const char *hex);   // back from an overhead card: that plane stays focused 30 s (M7)
bool mapFocusWillPop();                // the idle pause (09 M1b)

// Rain radar (docs/10-rain-radar.md)
void radarEnter();
void radarUpdate(uint32_t now);
void radarLeave();                     // releases the SD mount
bool radarTouchBack(int16_t x, int16_t y);
bool radarCue(char *out, size_t n);    // the weather screen's rain cue, same words as the strip

enum class SetupResult : uint8_t { None, Done };
void setupEnter();
SetupResult setupTouch(int16_t x, int16_t y);
void setupTick(uint32_t now);
void setupStartCal();          // hold 3 s inside Settings
bool setupInCalibration();
