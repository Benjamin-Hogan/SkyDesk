// app_state.h - POD data shared between the net task (core 0) and the UI
// (core 1). Writers lock, copy, bump a version, unlock. Readers copy a
// snapshot under the lock and draw from the copy. Never hold the lock while
// doing HTTP or drawing. See docs/03-architecture.md -> Threads.
#pragma once

#include <Arduino.h>
#include <time.h>
#include "config.h"

struct Aircraft {
  char hex[7];
  char callsign[9];        // trimmed
  char reg[10];
  char type[5];            // ICAO designator, e.g. "B738"
  char desc[28];           // adsb.fi "desc" (uppercase)
  char ownOp[28];          // adsb.fi "ownOp" (uppercase)
  char category[3];
  double lat, lon;
  int32_t altFt;           // MSL (geom if present, else baro)
  bool onGround;
  float gsKt, track;
  int16_t vRateFpm;
  float seenPos;           // s since last position, at fetch time
  // computed by the net task (geo.cpp)
  float distNm, azDeg, elDeg;
};

struct Traffic {
  Aircraft ac[MAX_AIRCRAFT];
  uint8_t n;
  uint16_t totalInRadius;  // before truncation to MAX_AIRCRAFT
  uint32_t fetchedMs;      // millis() of the last successful fetch
  bool ok;                 // last fetch succeeded
  uint8_t failStreak;      // consecutive failures (both providers)
  uint8_t provider;        // 0 primary (adsb.fi), 1 fallback (adsb.lol)
  uint32_t nextRetryMs;    // millis() of the next attempt
  uint32_t version;
};

struct RouteInfo {
  char hex[7];
  char callsign[9];
  bool found;              // adsbdb knew the aircraft
  bool hasRoute;           // adsbdb returned a flightroute
  char airline[28];        // flightroute.airline.name
  char flightIata[10];     // "WN1637"
  char manufacturer[20];
  char icaoType[5];
  char owner[28];          // registered_owner
  char origIata[4], destIata[4];
  char origCity[22], destCity[22];
  double oLat, oLon, dLat, dLon;
};

struct HourSlot {
  time_t t;
  int16_t tempF;
  uint8_t code;            // WMO weather code
  uint8_t pop;             // precip probability %
  bool isDay;
};

#define WX_HOURS 6

struct Weather {
  bool valid;
  time_t fetchedEpoch;
  uint32_t fetchedMs;
  int16_t tempF, feelsF, hiF, loF;
  uint8_t humidity;
  uint8_t code;
  bool isDay;
  uint16_t windMph, windDeg;
  HourSlot hourly[WX_HOURS];
  uint8_t nHourly;
  time_t sunrise[2], sunset[2];   // today, tomorrow
  uint32_t version;
};

enum class WifiPhase : uint8_t { Connecting, Connected, Failed };

struct NetStatus {
  WifiPhase wifi;
  char ssid[33];
  int8_t rssi;
  uint8_t attempt;
  uint32_t nextRetryMs;
  bool timeSynced;
  bool weatherTried;
  bool radarTried;
};

void appStateInit();

// Writers (net task)
void appSetTraffic(const Traffic &t);
void appSetWeather(const Weather &w);
void appSetNet(const NetStatus &n);

// Readers (UI) - copy out a consistent snapshot
void appGetTraffic(Traffic &out);
void appGetWeather(Weather &out);
void appGetNet(NetStatus &out);
uint32_t appTrafficVersion();
uint32_t appWeatherVersion();

// UI -> net task hints
enum class UiScreen : uint8_t { Boot, Weather, Plane, Setup, Map };
void appSetUiScreen(UiScreen s);
UiScreen appGetUiScreen();

// UI -> net task: ADS-B query radius and cadence (the map widens/speeds them).
void appSetPollPlan(uint8_t radiusNm, uint16_t intervalMs);
void appGetPollPlan(uint8_t &radiusNm, uint16_t &intervalMs);
// UI -> net task: the map's selected aircraft, never evicted by the 40 cap.
void appSetPinnedHex(const char *hex);   // "" to clear
void appGetPinnedHex(char out[7]);
