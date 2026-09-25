// adsb_parse.h - readsb aircraft JSON -> Aircraft[] (geometry computed).
#pragma once

#include <ArduinoJson.h>
#include "app_state.h"
#include "byte_source.h"

// Stream-parse a readsb response one aircraft object at a time (constant memory).
// Airborne aircraft only (ground traffic is dropped), sorted by distance; if more
// than `cap` arrive, the nearest `cap` are kept.
// Returns the count, -1 if there is no aircraft array, -2 if the stream broke off.
int adsbParseStream(ByteSource &in, Aircraft *out, size_t cap);

// Truncate a sorted list to `keep`, swapping the pinned (selected) hex into the
// last slot if it would otherwise be evicted. Returns the new count.
size_t adsbKeepNearest(Aircraft *buf, size_t n, size_t keep, const char *pinned);
