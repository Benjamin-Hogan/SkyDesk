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
  float elevM;                    // Open-Meteo's elevation of the point (NAN if absent)
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
  bool trafficTried;
  uint8_t lastReason;                 // last WiFi disconnect reason (boot words, docs/12)
  // diagnostics shown on screen while data is missing (there may be no serial link)
  int16_t lastHttp;                   // last failing HTTP status / HTTPClient error (0 = none)
  int32_t lastTls;                    // mbedTLS error of that failure (0 = none)
  uint16_t heapFreeK, heapMinK, heapLargestK;
  char wxErr[40];                     // why the last weather fetch failed
};

void appStateInit();

// Writers (net task)
// Traffic has ONE shared copy (RAM, v3: each copy is ~6 KB and TLS needs every byte),
// owned and written ONLY by the net task, and only between appTrafficLock()/Unlock(). The
// net task may read it without the lock (it is the only writer); the UI copies it with
// appGetTraffic(). appTrafficPublish() bumps the version after a change.
Traffic &appTrafficShared();
void appTrafficLock();
void appTrafficUnlock();
void appTrafficPublish();
void appSetWeather(const Weather &w);
void appSetNet(const NetStatus &n);

// Readers (UI) - copy out a consistent snapshot
void appGetTraffic(Traffic &out);
void appGetWeather(Weather &out);
void appGetNet(NetStatus &out);
uint32_t appTrafficVersion();
uint32_t appWeatherVersion();

// UI -> net task hints
enum class UiScreen : uint8_t { Boot, Weather, Plane, Setup, Map, Radar, Today };
void appSetUiScreen(UiScreen s);
UiScreen appGetUiScreen();

// UI -> net task: ADS-B query radius and cadence (the map widens/speeds them).
void appSetPollPlan(uint8_t radiusNm, uint16_t intervalMs);
void appGetPollPlan(uint8_t &radiusNm, uint16_t &intervalMs);
// UI -> net task: the map's selected aircraft, never evicted by the 40 cap.
void appSetPinnedHex(const char *hex);   // "" to clear
void appGetPinnedHex(char out[7]);

// ---- rain radar (docs/10-rain-radar.md) -----------------------------------
// Frames live on SD as /radar/<valid>.bin (radar_client.cpp). This is the index.
struct RadarStatus {
  bool sdOk;
  uint8_t n;                          // frames on SD, oldest first
  uint32_t valid[RADAR_FRAMES];       // unix time of each frame
  uint32_t newestValid;               // IEM's newest composite (n0q_0.json), 0 = unknown
  uint32_t checkedEpoch;              // last successful n0q_0.json check
  uint8_t failStreak;
  bool loading;                       // backfilling earlier frames right now
  bool lastFailed;                    // the last check or fetch failed (stale = WARN only then)
  bool unreadable;                    // the newest frame was rejected (colour check)
  uint8_t quorumPct;                  // radars reporting nationally (< 95 = partial coverage)
  char err[20];                       // why the last attempt failed ("http 503", "sd", "ram" ...) - on screen
  // words for the newest frame (visible area, cleaned)
  bool rain, heavy;                   // NAMED rain (words / cue)
  bool echo;                          // anything drawn (frame bar, loop, backfill: v3-R3-1)
  float rainMi, rainAz, heavyMi, heavyAz;
  uint32_t version;
};
uint32_t appSetRadar(const RadarStatus &r);      // returns the new version
void appGetRadar(RadarStatus &out);
uint32_t appRadarVersion();
// The UI acknowledges each frame list it has taken; the net task only deletes a frame
// file once a list without it has been acknowledged (it may be open for playback).
void appRadarAck(uint32_t version);
uint32_t appRadarAcked();
