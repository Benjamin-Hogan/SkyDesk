// radar_client.h - rain radar frames on SD (docs/10-rain-radar.md -> Data).
// Net task only, except the file-format definitions, which the UI reads.
#pragma once

#include <Arduino.h>
#include "radar_model.h"

// /radar/<valid>.bin = RadarFrameHdr + RADAR_W*RADAR_H/2 bytes of 4bpp levels
// (row-major, even x in the HIGH nibble, like TFT_eSprite 4-bit).
#define RADAR_DIR     "/radar"
#define RADAR_WET_W   (RADAR_W / 4)       // wet mask: 1 bit per 4x4 cell, any rain
#define RADAR_WET_H   (RADAR_H / 4)
#define RADAR_VIS_H   214                 // rows above the strip: the visible map
#define RADAR_MAGIC   0x31524B53u         // "SKR1"

struct RadarFrameHdr {
  uint32_t magic;
  uint32_t valid;                          // unix time (UTC) of the composite
  uint8_t rain, heavy;                     // words for this frame (visible area, cleaned)
  uint8_t echo;                            // anything drawn at all (named or not)
  uint8_t pad;
  float rainMi, rainAz, heavyMi, heavyAz;
  uint8_t wet[RADAR_WET_W * RADAR_WET_H / 8];
};

void radarFramePath(uint32_t valid, char *out, size_t n);

void radarClientInit();                    // after sdInit(): index what's on the card
// One step of radar work: at most ONE frame download + convert. `screenOpen`: the radar
// screen is up (fetch at once if stale, 2 min checks, backfill the loop). `wetHint`: the
// forecast makes rain plausible (10 min cue cadence instead of 30). Returns true if a
// frame was attempted - the caller then runs an ADS-B poll before the next one (v3-R1-10).
bool radarService(bool screenOpen, bool wetHint);
