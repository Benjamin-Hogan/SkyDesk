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

// "Will pop the card soon": does not qualify now, but qualifies at +15/+30/+45/+60 s
// along track, ground speed and vertical rate. Sampling the PATH matters: a fast
// jet can cross the trigger area inside a minute (review v2-R3-2).
// Returns the first passing time in seconds, or 0.
uint8_t mapWillPopSecs(const Aircraft &a);

// Airborne aircraft nearest to screen point (x,y) within `radiusPx`, or -1.
int mapHitTest(const Traffic &t, float pxPerNm, int16_t x, int16_t y, int16_t radiusPx);
