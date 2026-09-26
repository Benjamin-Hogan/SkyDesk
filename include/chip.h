// chip.h - the weather chip's arbiter (docs/11-today.md -> Surfaces). Pure, host-tested.
// One rule decides what the chip says; later features plug in above the passed row
// (feature 3: will-pop; 3.1: NWS warning). The chip opens the map in every state.
#pragma once

#include <stdint.h>
#include <stddef.h>

enum class ChipKind : uint8_t { Offline, Passed, Nearby, Quiet };

struct ChipInputs {
  bool trafficUp;
  int32_t retryS;              // offline: seconds to the next attempt
  uint16_t nNearby;            // airborne aircraft in the snapshot
  const char *nearestType;     // nullptr when there is none
  float nearestMi;
  const char *nearestDir;      // compass word
  bool hasPass;                // a closed overhead pass exists
  uint32_t passAgeS;           // since its CLOSE
  const char *passOp, *passType, *passIcao;
  const char *passCode;        // well-known IATA airline code ("BA"), "" otherwise
  const char *passHex;
};

struct ChipMsg {
  ChipKind kind;
  char left[24];
  char right[48];
  bool chevron;                // tappable (-> map)
  char focusHex[7];            // passed: the map focuses this plane ("" = nearest as usual)
};

// Pixel width of an f2 string (device: TFT textWidth; tests: a fixed-pitch stub).
using ChipWidthFn = int16_t (*)(const char *);

// Chip geometry (screens.py weather()): left text at x 36, right text ends at x 296.
#define CHIP_LEFT_X   36
#define CHIP_RIGHT_X  296
#define CHIP_GAP      12

void chipMessage(const ChipInputs &in, ChipWidthFn width, ChipMsg &out);

// The weather header's Today entry (docs/11 -> Surfaces 2), left of any degraded status:
// "31 overhead", else "31", else "" (the tap target stays). `dateEndX` = right edge of the
// date text; `statusLeftX` = left edge of the status (SCREEN_W when none). The entry
// starts HEADER_SEP after the date (a drawSep dot sits in that gap) and must end, with its
// chevron, CHIP_GAP before the status. Returns false when nothing fits.
#define HEADER_SEP    16
#define HEADER_CHEV_W 11
bool headerEntryFit(uint16_t overhead, int16_t dateEndX, int16_t statusLeftX, ChipWidthFn width, char out[16]);
