// spotter.h - Today's Sky model (docs/11-today.md): overhead passes, planes nearby,
// hourly bars, rarest type, the last passes, and the Heads-up promise log.
// Pure logic: no SD, NVS, drawing or network (host-tested). Fed on core 1 after
// trackerUpdate(); the device glue (today_store.cpp) drains the outbox to SD from the net
// task and restores the summary at boot. The glue serialises all calls with one mutex.
#pragma once

#include "app_state.h"
#include "tracker.h"

#define TODAY_RECENT     3      // passes kept for the chip and the Today page
#define TODAY_OPEN       4      // passes open at once (overhead right now)
#define TODAY_PEND       3      // pushed out of `recent` but still inside the merge window
#define TODAY_PROMISES   8      // live Heads-up promises
#define TODAY_JUDGED     8      // hexes whose promise was judged recently (first promise wins)
#define TODAY_TYPEQ      16     // type sightings waiting for the SD lookup
#define TODAY_PLOG       6      // resolved promises waiting to be logged
#define TODAY_SET_BITS   4096   // nearby hash set (512 B)
#define TODAY_GATE_DAYS  7

// Words for one pass. Never an owner name: the operator comes only from adsbdb's flight
// route airline or the built-in airline table (not registered_owner / ownOp).
struct PassLabel {
  char op[20];        // "Southwest"; "" for GA / unknown
  char type[22];      // "737 MAX 8", "Piper PA-28"
  char icao[5];       // "B38M"
  char reg[10];       // "N4312K"
  char callsign[9];
  char code[3];       // well-known IATA airline code ("BA") for the row's short form
  char orig[4], dest[4];   // "" when no PLAUSIBLE route (routePlausible)
};

struct PassRec {
  char hex[7];
  bool logged;          // its CSV line has been handed out
  uint8_t toward;       // compass octant (0 = N ... 7 = NW) of the track at the peak
  uint32_t day;         // local yyyymmdd of its ENTER
  uint32_t peakEpoch;   // unix time of the highest elevation
  uint32_t closeEpoch;  // unix time it closed (chip age, merge window); 0 = still open
  uint32_t seenMs;      // open passes: millis() it was last in the snapshot (lost backstop)
  float peakEl, peakNm;
  PassLabel lab;
};

enum class PromiseOutcome : uint8_t { Kept, Early, Late, Missed, Excluded };

struct PromiseLine {    // one resolved promise, for the CSV
  char hex[7];
  PromiseOutcome outcome;
  uint16_t leadS;
  int16_t errS;         // ENTER - due (s); 0 when it never entered
  uint32_t madeEpoch;
};

struct DayLine {        // written at midnight to days.csv
  uint32_t dayKey;      // yyyymmdd
  uint16_t overhead, nearby;
  char rarest[5];
  uint16_t kept, broken, excluded;
};

struct PromiseDay { uint16_t kept, broken, excluded; };

// Everything the Today page needs, and everything persisted in NVS (a POD blob).
struct TodaySummary {
  uint32_t magic;                  // TODAY_MAGIC: layout version for the NVS blob
  uint32_t dayKey;                 // yyyymmdd of the counts below, 0 = none yet
  uint16_t overhead;
  uint16_t nearbyBase;             // nearby carried over when the set was lost (no SD)
  uint16_t nearbySaved;            // the estimate at its last change (restored without the set)
  uint8_t hourly[24];              // overhead passes per local hour
  char rarest[5];                  // ICAO type, "" = none
  uint16_t rarestSightings;        // its past sightings (lower = rarer)
  uint16_t historyDays;            // days completed WITH the log running (learning < 3)
  PassRec recent[TODAY_RECENT];    // newest first
  uint8_t nRecent;
  PromiseDay today;                // promises resolved today
  PromiseDay ring[TODAY_GATE_DAYS];// the previous 7 days, [0] = yesterday
  uint16_t dropped;                // outbox overflow / failed writes (lines lost), reported
  uint32_t outageHours;            // bit h: > TODAY_OUTAGE_MIN of traffic outage in hour h
  bool nearbyLost;                 // the set was lost (reboot without SD): "~212"
  bool loggedToday;                // the log ran today (counts toward historyDays)
};
#define TODAY_MAGIC 0x54445933u    // "TDY3" - bump on ANY layout change (NVS blob)

struct SpotterClock {
  bool valid;           // NTP synced (nothing is counted before)
  uint32_t epoch;       // unix time
  uint32_t dayKey;      // local yyyymmdd
  uint8_t hour;         // local hour 0-23
};

// Resolves the words for an aircraft (device: routeGet + spotterLabels).
using SpotterLabelFn = void (*)(const Aircraft &a, PassLabel &out);

// The words for a pass (type from planeLabels, operator from the route's airline or the
// airline table only - never an owner) + a route only if plausible.
void spotterLabels(const Aircraft &a, const RouteInfo *route, PassLabel &out);

// CSV lines (ASCII, commas in names become spaces, no owner field exists). `fmt`
// formats a unix time as "HH:MM" local; returns the line length.
using SpotterTimeFn = void (*)(uint32_t epoch, char out[6]);
size_t spotterCsvPass(const PassRec &p, SpotterTimeFn fmt, char *buf, size_t n);
size_t spotterCsvPromise(const PromiseLine &l, SpotterTimeFn fmt, char *buf, size_t n);
size_t spotterCsvDay(const DayLine &d, char *buf, size_t n);
#define SPOTTER_CSV_PASS_HEADER "time,hex,callsign,reg,type,operator,orig,dest,peak_el,peak_mi,toward"
#define SPOTTER_CSV_PROMISE_HEADER "made,hex,lead_s,outcome,err_s"
#define SPOTTER_CSV_DAY_HEADER "date,overhead,nearby,rarest,kept,broken,excluded"
bool spotterJunkType(const char *icao, const char *category);

void spotterInit(SpotterLabelFn labels);

// The log is being kept (the SD card works). "learning" counts only such days.
void spotterSetLogging(bool on);

// Core 1, once per UI tick after trackerUpdate(): drains the tracker's pass events,
// follows open passes, feeds the nearby set and the promise log. `newPoll` = the traffic
// snapshot changed since the last call; `trafficUp` = the chip's notion (not offline).
void spotterUpdate(const Traffic &t, bool newPoll, bool trafficUp, const SpotterClock &c, uint32_t nowMs);

const TodaySummary &spotterSummary();
uint32_t spotterVersion();                     // bumps whenever the summary changes
uint32_t spotterPersistVersion();              // bumps on changes worth an NVS write (not nearby)
uint16_t spotterNearby();                      // estimate: nearbyBase + linear counting
bool spotterLastPass(PassRec &out);            // newest closed pass (the chip)

// Net task (via the glue): the outbox.
bool spotterTakePassLine(PassRec &out, uint32_t nowEpoch);   // oldest pass past its merge window
bool spotterTakePromiseLine(PromiseLine &out);
bool spotterTakeTypeQuery(char out[5]);        // a type newly seen today
bool spotterTakeDayLine(DayLine &out);
void spotterLineLost();                        // a handed-out line could not be written
// Answer to a type query: the type's sightings and distinct days BEFORE `dayKey` (the day
// the query was answered for; an answer for another day is ignored).
void spotterTypeAnswer(const char *icao, uint16_t sightingsBefore, uint16_t daysBefore, uint32_t dayKey);

// Persistence (glue): restore at boot; the set is optional (no SD / wrong day -> nullptr).
void spotterRestore(const TodaySummary &s, const uint8_t *set, size_t setLen);
const uint8_t *spotterSet(size_t &len);
bool spotterSetDirty(bool clear);              // set changed since the last save

// Heads-up gate (feature 3): kept / (kept + broken) over the ring + today, >= minN.
// Returns the percentage, or -1 when there are fewer than minN promises.
int spotterGatePct(uint16_t minN, uint16_t *n = nullptr, uint16_t *excluded = nullptr);

// Pure helpers (exposed for tests)
uint16_t spotterSetHash(const char *hex);      // 12-bit slot of a hex id
uint16_t spotterLinearCount(uint16_t bitsSet, uint16_t bits);
uint8_t spotterOctant(float trackDeg);
int32_t spotterDaysBetween(uint32_t fromKey, uint32_t toKey);   // yyyymmdd -> whole days
