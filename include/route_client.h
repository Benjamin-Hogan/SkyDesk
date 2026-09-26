// route_client.h - adsbdb aircraft + route lookup, LRU cache, plausibility.
// docs/04-data-sources.md §2.
#pragma once

#include "app_state.h"

void routeInit();

// Net task: true if this aircraft has no fresh cache entry yet.
bool routeNeeded(const Aircraft &a);
// Net task: a slot can take this aircraft without evicting a plane still within
// LOOKUP_RADIUS_NM (else skip the lookup: no re-fetch loops).
bool routeHasRoom(const Aircraft &a, const Traffic &t);
// Net task: blocking HTTPS lookup; stores the result (found or not) in cache.
void routeLookup(const Aircraft &a);

// UI: copy the cached entry for `hex`. False if not looked up yet.
bool routeGet(const char *hex, RouteInfo &out);

// Schedules lie: is this route consistent with where the aircraft is now?
bool routePlausible(const RouteInfo &r, const Aircraft &a);
