// map_model.h - pure logic for the plane map (docs/08-plane-map.md):
// projection (identical to tools/basemap/make_basemap.py), trail history,
// hit-testing. No drawing, no I/O -> host-tested.
#pragma once

#include "app_state.h"
#include "basemap.h"

// Observer-centred equirectangular projection at `pxPerNm`.
void mapProject(double lat, double lon, float pxPerNm, float &x, float &y);

#define MAP_TRAIL_N  8
#define MAP_TRAILS   MAX_AIRCRAFT

struct Trail {
  char hex[7];
  float lat[MAP_TRAIL_N], lon[MAP_TRAIL_N];
  uint8_t n;          // valid points
  uint8_t head;       // index of the newest point
  uint32_t seenMs;    // last time this hex was in the feed
  bool used;
};

// Append the airborne aircraft in `t` (one point per new position) and expire
// trails whose hex has been missing for 60 s. Call once per traffic version.
void trailsUpdate(const Traffic &t, uint32_t nowMs);
const Trail *trailFor(const char *hex);

// v1 ENTER test (docs/01): <= 3 nm AND >= 25 deg up AND >= 300 ft above ground.
bool mapQualifies(double distNm, double elDeg, double altFt);

// "Will pop the card soon": does not qualify now, but qualifies somewhere on its
// path within 60 s, flown along track, ground speed and vertical rate. v3 samples every
// MAP_POP_STEP_S (5 s, docs/09 M1a): a fast jet can cross the trigger area inside a
// minute (review v2-R3-2), and the first hit is also the drawn pop point (M2).
struct WillPop { uint8_t secs; double lat, lon; };
bool mapWillPop(const Aircraft &a, WillPop &out);
uint8_t mapWillPopSecs(const Aircraft &a);   // first passing time in seconds, or 0

// Position `secs` ahead along track/ground speed (the 60 s leader end, M2).
void mapAhead(const Aircraft &a, uint16_t secs, double &lat, double &lon);

// Focus (docs/09 M1/M7): selected > held will-pop > soonest will-pop > just popped
// (afterPopHex, 30 s) > -1 (the caller then uses the nearest plane on the map). A will-pop
// focus is HELD until it stops being will-pop for 2 polls in a row or leaves the traffic
// - it is not swapped poll-to-poll for a slightly sooner plane. newPoll=true once per snapshot.
struct FocusHold { char hex[7]; uint8_t misses; };
int mapPickFocus(const Traffic &t, const char *selectedHex, const char *afterPopHex, FocusHold &hold,
                 bool newPoll);

// Map idle (docs/09 M1b, v3-R3-2): MAP_IDLE_S after timerStartMs (a touch OR a return
// from a card) returns to weather, paused while a will-pop plane is the focus - but never
// beyond MAP_IDLE_MAX_S since the last REAL touch (card returns don't reset that one).
bool mapIdleExpired(uint32_t nowMs, uint32_t timerStartMs, uint32_t lastTouchMs, bool willPopFocus);

// Tap-again cycling (docs/09 M3). The candidates (planes within MAP_TAP_RADIUS_PX of the
// tap, by distance from the tap) are FROZEN as hex codes with the tap point as ANCHOR. A
// tap within MAP_TAP_RADIUS_PX of the anchor, while the focus is in the list, advances
// (v3-R2-3); any other tap starts a new list. Taps < 250 ms apart are one tap (bounce).
struct TapCycle { char hex[8][7]; uint8_t n, idx; uint32_t lastMs; int16_t ax, ay; };
// Returns the traffic index to select, -1 to clear the selection, -2 to ignore (bounce).
// k/n: position in the frozen list for the strip's "k of N here" (n <= 1: nothing to show).
int mapTap(const Traffic &t, float pxPerNm, int16_t x, int16_t y, uint32_t nowMs, const char *focusHex,
           TapCycle &c, uint8_t &k, uint8_t &n);

// "900" / "7.2k" / "12k" (+ " ft").
void mapAltTag(int32_t ft, char *out, size_t n, bool unit);

// Strip right side (docs/09 -> Strip): the first matching state, then the first fallback
// that leaves room for the WHOLE operator (opW) + type (typeW); else the first leaving a
// 40 px stub; else the last. The type is never truncated. Right edge 300, left text at 26.
enum class StripKind : uint8_t { Cycle, WillPop, Passed, InDisc, Default };
struct StripSeg { char text[24]; uint8_t pal; };      // MapPal colour index
typedef int16_t (*TextWidthFn)(const char *);
uint8_t mapStripRight(StripKind kind, const Aircraft &a, uint8_t k, uint8_t n, uint8_t popSecs, int16_t typeW,
                      int16_t opW, TextWidthFn width, StripSeg out[2], bool *preferred = nullptr);
// *preferred: the state's first option was kept (only then may the flight number be added).

// Tap-again cycling (M3): the airborne planes within radiusPx of the TAP, ordered by
// distance from the tap. Writes up to `cap` indices into `out`; returns the count.
uint8_t mapTapCandidates(const Traffic &t, float pxPerNm, int16_t x, int16_t y, int16_t radiusPx,
                         int8_t *out, uint8_t cap);

// Airborne aircraft nearest to screen point (x,y) within `radiusPx`, or -1.
int mapHitTest(const Traffic &t, float pxPerNm, int16_t x, int16_t y, int16_t radiusPx);
