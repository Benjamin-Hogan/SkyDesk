// trails_log.h - Sky Trails recorder format (docs/13-sky-trails.md -> Step 1). Pure: the
// encoder/decoder and the honesty filter are host-tested; trails_store.cpp does the SD I/O.
//
// A day file /skydesk/trails/YYYY-MM-DD.bin:
//   16 B header: "SKT1", u32 dayKey (yyyymmdd), i32 buildLat e6, i32 buildLon e6
//   per poll:     u8 0xA5, u8 n, u16 secOfDay/2
//                 n x 8 B points: u8 hex[3] (ICAO), i16 dx, i16 dy (0.0005 deg from the build
//                 centre), u8 alt (ft MSL / 200)
// Little-endian (ESP32 and x86 both).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "app_state.h"

#define TRAILS_MAGIC        "SKT1"
#define TRAILS_HDR_BYTES    16
#define TRAILS_REC_MARK     0xA5
#define TRAILS_POINT_BYTES  8
#define TRAILS_DEG_UNIT     0.0005
#define TRAILS_ALT_UNIT_FT  200

struct TrailsPoint {
  uint32_t icao;      // 24-bit address
  double lat, lon;
  int32_t altFt;
};

// Honesty (Vega): a fresh ADS-B/MLAT position of an airborne aircraft, not TIS-B ('~' hex).
bool trailsKeep(const Aircraft &a);

void trailsHeader(uint32_t dayKey, double buildLat, double buildLon, uint8_t out[TRAILS_HDR_BYTES]);
// One poll record: the kept aircraft of `ac` (up to 255), relative to the build centre.
// Returns the bytes written (0 if nothing was kept or `cap` is too small for even the header).
size_t trailsEncodePoll(const Aircraft *ac, uint8_t n, uint32_t secOfDay, double buildLat, double buildLon,
                        uint8_t *out, size_t cap);

// Decoding (host tests, the mock tools, and later the screen): walk records in a buffer.
struct TrailsRec { uint32_t secOfDay; uint8_t n; const uint8_t *points; };
// Next record starting at `off`; false at the end or on corruption (resync is the caller's job).
bool trailsNextRec(const uint8_t *buf, size_t len, size_t &off, TrailsRec &out);
TrailsPoint trailsPoint(const TrailsRec &r, uint8_t i, double buildLat, double buildLon);
