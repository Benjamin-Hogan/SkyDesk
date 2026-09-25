// radar_model.h - pure logic for the rain radar (docs/10-rain-radar.md): TIFF
// header parsing, n0q colour -> rain level, clutter cleaning, nearest rain.
// No I/O: rows come through the RadarRows interface, so the device runs this over
// SD files and the host tests over memory.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "config.h"

// ---- geometry of the radar view (== docs/mockups/radar_data.py) ----------
#define RADAR_W        320
#define RADAR_H        240
#define RADAR_CX       160
#define RADAR_CY       116
#define RADAR_RING_MI  50
#define RADAR_PPN      (100.0f / (RADAR_RING_MI / 1.150779f))   // px per nm (2.30)

// ---- n0q colour table (generated: firmware/radar_table.cpp) ---------------
struct RadarRamp { uint32_t rgb; int16_t dbz2; };   // dBZ x 2
extern const RadarRamp RADAR_RAMP[];
extern const uint16_t RADAR_RAMP_N;

// dBZ x2 of an EXACT n0q colour, or RADAR_MISS. Never guesses (v3-R1-1): a frame whose
// echo pixels miss more than RADAR_MISS_PERMILLE is rejected by the caller.
#define RADAR_MISS INT16_MIN
int16_t radarDbz2(uint8_t r, uint8_t g, uint8_t b);
// Rain level 0 (none) .. 5 from a TIFF pixel. Alpha 0 = no echo. A colour that is not
// in the n0q table gives 0 and sets *miss (if given).
uint8_t radarLevel(uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool *miss = nullptr);

// Static ground clutter (generated: firmware/radar_clutter.cpp, 1 bit per pixel):
// RADAR_CLUTTER = the mask, RADAR_CLUTTER_NEAR = within 2 px of it. Applied PER BLOB by
// radarClean (v3-R2-2): a real storm crossing a mask site keeps every pixel.
extern const uint8_t RADAR_CLUTTER[RADAR_W * RADAR_H / 8];
extern const uint8_t RADAR_CLUTTER_NEAR[RADAR_W * RADAR_H / 8];
inline bool radarIsClutter(uint16_t x, uint16_t y) {
  const uint32_t i = (uint32_t)y * RADAR_W + x;
  return (RADAR_CLUTTER[i >> 3] >> (i & 7)) & 1;
}
inline bool radarNearClutter(uint16_t x, uint16_t y) {
  const uint32_t i = (uint32_t)y * RADAR_W + x;
  return (RADAR_CLUTTER_NEAR[i >> 3] >> (i & 7)) & 1;
}

// A level byte may carry RADAR_UNNAMED: the pixel belongs to a blob that is drawn but
// never NAMED by the strip words or the weather cue (too small / too weak to matter).
#define RADAR_UNNAMED  0x80
#define RADAR_LV(v)    ((uint8_t)((v) & 0x07))

// ---- TIFF (uncompressed, 8-bit, 4 samples, chunky or planar strips) -------
struct TiffInfo {
  uint16_t w, h;
  bool planar;               // PlanarConfiguration 2: all R, then G, B, A
  uint16_t rowsPerStrip;
  uint16_t nStrips;
  uint32_t stripOff[64];
  uint32_t stripCnt[64];     // StripByteCounts (0 if absent)
};
// Parse from the first bytes of the file (IFD + arrays must lie inside buf).
bool tiffParse(const uint8_t *buf, size_t n, TiffInfo &ti);
// File offset of pixel (x=0, y) in sample plane p (0..3), and the byte step between x.
uint32_t tiffRowOffset(const TiffInfo &ti, uint8_t plane, uint16_t y, uint8_t &step);
// Bytes a complete file must have: the end of the last strip. IEM sends no Content-Length
// (HTTP/1.0, Connection: close), so this is how a download proves it is complete.
uint32_t tiffDataEnd(const TiffInfo &ti);

// The loop set for newest frame T (reviewer H2): T itself plus the newest frames on a FIXED
// 10-minute grid (valid % RADAR_FRAME_STEP_S == 0) - IEM steps by 5 min, and a set anchored
// to T would re-download the whole loop every 5 minutes. Oldest first; returns the count.
uint8_t radarLoopSet(uint32_t newest, uint32_t *out, uint8_t frames, uint32_t step);

// ---- rows of rain levels (one byte per pixel, 0..5) -----------------------
class RadarRows {
 public:
  virtual ~RadarRows() = default;
  virtual bool read(uint16_t y, uint8_t *row) = 0;          // w bytes
  virtual bool write(uint16_t y, const uint8_t *row) = 0;
};

// Clean a layer in place, per 8-connected BLOB (docs/10 -> Clutter, v3-R2-2):
//  - drop a blob smaller than minBlobPx, or with >= half its pixels near the clutter
//    mask (nearMask; nullptr = no mask);
//  - flag RADAR_UNNAMED on a surviving blob that is not significant: significant =
//    >= RADAR_NAME_MIN_PX with moderate+ rain, or >= RADAR_NAME_LIGHT_PX of any rain,
//    or ANY size within RADAR_NAME_ALWAYS_MI of you (v3-R3-1).
// Two passes of run-length union-find; pass 2 recomputes identical labels, so no label
// image is stored (about 14 KB transient). Returns false (layer untouched) if there are
// more runs than it can track (a real monsoon frame has about 830).
typedef bool (*RadarMaskFn)(uint16_t x, uint16_t y);
// Returns RADAR_CLEAN_OK, RADAR_CLEAN_OVERFLOW (too many runs: layer untouched) or
// RADAR_CLEAN_NOMEM (allocation failed: layer untouched - the caller must NOT publish it,
// an uncleaned frame would let clutter drive the words).
enum { RADAR_CLEAN_NOMEM = -1, RADAR_CLEAN_OVERFLOW = 0, RADAR_CLEAN_OK = 1 };
// blobMem/runMem: optional caller scratch (8 B per blob label; 2 x 161 x 12 B of runs), so
// the device needs no large allocation; without them it allocates (~16 KB).
int radarClean(RadarRows &rows, uint16_t w, uint16_t h, uint16_t minBlobPx, RadarMaskFn nearMask,
               void *blobMem = nullptr, size_t blobLen = 0, void *runMem = nullptr, size_t runLen = 0);
#define RADAR_CLEAN_RUN_BYTES (2 * (RADAR_W / 2 + 1) * 12)

// Nearest NAMED pixel with level >= minLevel above row maxY (the visible map area).
// Distance in statute miles, azimuth in degrees. False if none.
bool radarNearest(RadarRows &rows, uint16_t w, uint16_t maxY, uint8_t minLevel, float &miles, float &azDeg);
