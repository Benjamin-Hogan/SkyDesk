// trails_store.h - the Sky Trails recorder on the device (docs/13-sky-trails.md -> Step 1).
#pragma once

#include "app_state.h"

// Net task only, right after an ADS-B parse (before the screen cap): append the honest
// positions of this poll to today's /skydesk/trails file. A no-op without SD or a clock.
void trailsRecord(const Aircraft *ac, uint8_t n);
