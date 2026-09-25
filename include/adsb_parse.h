// adsb_parse.h - readsb aircraft JSON -> Aircraft[] (geometry computed).
#pragma once

#include <ArduinoJson.h>
#include "app_state.h"

void adsbBuildFilter(JsonDocument &f);
// Airborne aircraft only (ground traffic is dropped), sorted by distance.
// If more than `cap` arrive, the nearest `cap` are kept. -1 = no aircraft array.
int adsbParse(const JsonDocument &doc, Aircraft *out, size_t cap);

// Truncate a sorted list to `keep`, swapping the pinned (selected) hex into the
// last slot if it would otherwise be evicted. Returns the new count.
size_t adsbKeepNearest(Aircraft *buf, size_t n, size_t keep, const char *pinned);
