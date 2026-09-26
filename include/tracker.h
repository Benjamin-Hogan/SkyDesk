// tracker.h - decides whether the Plane screen is up and which aircraft it
// features. Pure logic over a Traffic snapshot + millis(); no drawing, no I/O.
// Rules: docs/01-product-spec.md -> Core behavior; state chart:
// docs/03-architecture.md -> Screen state machine.
#pragma once

#include "app_state.h"

enum class PlaneMode : uint8_t {
  None,       // show weather
  Live,       // a qualifying plane is featured
  Departing,  // nothing qualifies; grace/dwell countdown before weather
  Forced,     // user tapped the traffic chip: nearest plane, not overhead
};

struct TrackView {
  PlaneMode mode;
  Aircraft ac;            // featured aircraft (latest known copy)
  bool stale;             // Live but no fresh position for STALE_AFTER_S
  uint8_t ageS;           // seconds since its last fresh position
  uint8_t extra;          // other qualifying aircraft ("+N more")
  Aircraft others[3];     // up to 3 others, for hollow dots on the dome
  uint8_t nOthers;
  uint8_t graceLeftS;     // Departing
  uint8_t graceTotalS;
  uint8_t forcedLeftS;    // Forced
};

void trackerUpdate(const Traffic &t, uint32_t nowMs);
const TrackView &trackerView();

// true once per arrival (weather -> plane), for the arrival cue
bool trackerTakeArrival();

// Overhead-pass events for Today's Sky (docs/11-today.md): Enter when an aircraft passes
// the ENTER test (a Forced / tapped plane never does), Leave on EXIT or lost. Queued in
// order; the spotter drains them right after trackerUpdate().
struct PassEvent {
  bool enter;
  char hex[7];
};
bool trackerTakePassEvent(PassEvent &out);

// Touch actions
void trackerTapNext();
void trackerDismiss(uint32_t nowMs);
bool trackerForceNearest(const Traffic &t, uint32_t nowMs);
bool trackerIsDismissed(const char *hex);   // map draws these dim amber
bool trackerForceHex(const Traffic &t, const char *hex, uint32_t nowMs);   // tap on the map
