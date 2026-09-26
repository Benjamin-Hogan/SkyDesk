// aircraft_names.h - turn raw feed/adsbdb fields into the words on the Plane
// card header (docs/06-ui-spec.md §4.1, docs/04-data-sources.md §Display names).
#pragma once

#include "app_state.h"

struct PlaneLabels {
  char op[28];        // "Southwest"; empty for private/GA
  char type[28];      // "737-800" (with operator) or "Cessna 172" (without)
  char line2a[16];    // "WN 2208" or "N172SP"
  char line2b[28];    // "N8563Z" or "Private"
  bool airline;       // true -> header is "<op> . <type>"
};

// `route` may be null if the lookup hasn't completed.
void planeLabels(const Aircraft &a, const RouteInfo *route, PlaneLabels &out);

// ICAO designator -> {manufacturer, short model}. False if unknown.
bool typeLookup(const char *icao, const char *&mfr, const char *&model);

// Callsign prefix -> airline name from the built-in table ("SWA1637" -> "Southwest").
// Never an owner. False (out = "") when the prefix isn't a known airline.
bool airlineByCallsign(const char *callsign, char *out, size_t n);

// "SOUTHWEST AIRLINES CO" -> "Southwest"
void shortOperator(const char *in, char *out, size_t n);
