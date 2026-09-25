// weather_client.h - Open-Meteo current + next hours + today/tomorrow sun.
// docs/04-data-sources.md §3. Keyless.
#pragma once

#include "app_state.h"

bool weatherFetch(Weather &w);

// WMO code -> icon kind + label (used by the UI).
enum class WxKind : uint8_t { Clear, Partly, Cloud, Fog, Drizzle, Rain, Snow, Storm };
WxKind wxKind(uint8_t code);
const char *wxLabel(uint8_t code, bool isDay);
