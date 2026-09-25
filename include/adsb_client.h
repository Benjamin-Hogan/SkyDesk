// adsb_client.h - nearby aircraft from adsb.fi (primary) / adsb.lol (fallback).
// docs/04-data-sources.md §1.
#pragma once

#include "app_state.h"

// Fetch aircraft within POLL_RADIUS_NM of the observer into `t` (sorted by
// ground distance, geometry computed). Updates t.ok / failStreak / provider.
// Returns true on success. Handles failover internally.
bool adsbFetch(Traffic &t);

// The parse buffer, idle between fetches (net task only): the radar convert borrows it.
uint8_t *adsbScratch(size_t &len);
