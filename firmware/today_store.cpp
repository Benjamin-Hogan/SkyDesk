#include "today_store.h"

#include <Preferences.h>
#include <time.h>

#include "config.h"
#include "route_client.h"
#include "sd_store.h"

// Threads: spotter state is touched from core 1 (todayUpdate, snapshots) and core 0
// (todayService). Every spotter call happens under g_mx; SD work happens under sdLock()
// only, never while holding g_mx (the UI must never wait on the card).

namespace {

SemaphoreHandle_t g_mx = nullptr;
uint32_t g_savedVersion = 0;               // spotterPersistVersion() at the last NVS save
uint16_t g_savedNearby = 0;
uint32_t g_lastNvsMs = 0;
uint8_t  g_sdFails = 0;                     // consecutive Today write failures
bool     g_sdDead = false;                  // given up on the card for Today (reported)
uint32_t g_lastSetMs = 0;
bool g_clockValid = false;

// RAM (docs/11 -> RAM, measured on the device): no static copies. The net task's
// summary / set copies and todayInit()'s restore buffers live on the task stacks.
struct SetFile {
  uint32_t dayKey;                            // today.bit carries its day: a set from
  uint8_t bits[TODAY_SET_BITS / 8];           // yesterday is never restored as today's
};

#define TODAY_DIR     "/skydesk/log"
#define TODAY_SET     "/skydesk/today.bit"
#define TODAY_TYPES   "/skydesk/types.bin"
#define NVS_NS        "today"
#define NVS_KEY       "sum"

struct TypeRec {               // types.bin: lifetime sightings per ICAO type
  char type[4];
  uint16_t sightings;          // aircraft-days
  uint16_t days;               // distinct days seen
  uint32_t lastDay;            // yyyymmdd
};

void lock() { xSemaphoreTake(g_mx, portMAX_DELAY); }
void unlock() { xSemaphoreGive(g_mx); }

bool clockNow(SpotterClock &c) {
  const time_t now = time(nullptr);
  c = {};
  if (now < 1700000000) return false;
  struct tm lt;
  localtime_r(&now, &lt);
  c.valid = true;
  c.epoch = (uint32_t)now;
  c.dayKey = (uint32_t)((lt.tm_year + 1900) * 10000 + (lt.tm_mon + 1) * 100 + lt.tm_mday);
  c.hour = (uint8_t)lt.tm_hour;
  return true;
}

void hhmm(uint32_t epoch, char out[6]) {
  const time_t t = epoch;
  struct tm lt;
  localtime_r(&t, &lt);
  snprintf(out, 6, "%02d:%02d", lt.tm_hour, lt.tm_min);
}

void labels(const Aircraft &a, PassLabel &out) {   // core 1: routeGet copies under its own lock
  RouteInfo r;                                    // on the loop stack (8 KB): no static copy
  const bool have = routeGet(a.hex, r) && r.found;
  spotterLabels(a, have ? &r : nullptr, out);
}

// Append one line (+ the header on a new file). Caller holds sdLock.
bool append(const char *path, const char *header, const char *line) {
  File32 f = sdFs().open(path, O_WRONLY | O_CREAT | O_APPEND);
  if (!f) return false;
  bool ok = true;
  if (f.fileSize() == 0) ok = f.write(header, strlen(header)) == strlen(header) && f.write("\n", 1) == 1;
  ok = ok && f.write(line, strlen(line)) == strlen(line) && f.write("\n", 1) == 1;
  f.close();
  return ok;
}

void logPath(char *out, size_t n, uint32_t dayKey, const char *suffix) {
  snprintf(out, n, TODAY_DIR "/%04lu-%02lu-%02lu%s.csv", (unsigned long)(dayKey / 10000),
           (unsigned long)(dayKey / 100 % 100), (unsigned long)(dayKey % 100), suffix);
}

uint32_t dayOf(uint32_t epoch) {
  const time_t t = epoch;
  struct tm lt;
  localtime_r(&t, &lt);
  return (uint32_t)((lt.tm_year + 1900) * 10000 + (lt.tm_mon + 1) * 100 + lt.tm_mday);
}

// types.bin lookup + update. Caller holds sdLock. Returns false if the card failed.
bool typeSighting(const char *type, uint32_t today, uint16_t &sightBefore, uint16_t &daysBefore) {
  File32 f = sdFs().open(TODAY_TYPES, O_RDWR | O_CREAT);
  if (!f) return false;
  TypeRec r;
  char key[4] = {0, 0, 0, 0};
  memcpy(key, type, strnlen(type, 4));
  uint32_t pos = 0;
  while (f.read(&r, sizeof(r)) == (int)sizeof(r)) {
    if (memcmp(r.type, key, 4) == 0) {
      sightBefore = r.sightings;
      daysBefore = r.lastDay == today ? (uint16_t)(r.days ? r.days - 1 : 0) : r.days;
      if (r.sightings < 0xFFFF) r.sightings++;
      if (r.lastDay != today && r.days < 0xFFFF) r.days++;
      r.lastDay = today;
      f.seekSet(pos);
      const bool ok = f.write(&r, sizeof(r)) == sizeof(r);
      f.close();
      return ok;
    }
    pos += sizeof(r);
  }
  if (pos >= 1024 * sizeof(TypeRec)) {           // full: answer "common" rather than grow
    f.close();
    sightBefore = daysBefore = 0xFFFF;
    return true;
  }
  // A torn record (power cut mid-write) leaves the file off the 12-byte grid: append at the
  // last whole record and cut the tail, so every later lookup still lines up.
  f.seekSet(pos);
  f.truncate(pos);
  memcpy(r.type, key, 4);
  r.sightings = 1;
  r.days = 1;
  r.lastDay = today;
  const bool ok = f.write(&r, sizeof(r)) == sizeof(r);
  f.close();
  sightBefore = daysBefore = 0;
  return ok;
}

}  // namespace

void todayInit() {
  g_mx = xSemaphoreCreateMutex();
  spotterInit(labels);
  TodaySummary s;                                 // on the setup stack (~450 B)
  Preferences p;
  p.begin(NVS_NS, true);
  const size_t got = p.getBytes(NVS_KEY, &s, sizeof(s));
  p.end();
  SetFile setf;                                   // on the setup stack (~520 B)
  uint8_t *set = setf.bits;
  size_t setLen = 0;
  if (sdOk() && sdLock(UINT32_MAX)) {
    sdFs().mkdir(TODAY_DIR);
    File32 f = sdFs().open(TODAY_SET, O_RDONLY);
    if (f) {
      if (f.read(&setf, sizeof(setf)) == (int)sizeof(setf) && got == sizeof(s) && setf.dayKey == s.dayKey)
        setLen = sizeof(setf.bits);             // only today's set
      f.close();
    }
    sdUnlock();
  }
  if (got == sizeof(s)) spotterRestore(s, setLen ? set : nullptr, setLen);
  spotterSetLogging(sdOk());
  g_savedVersion = spotterPersistVersion();
  g_savedNearby = spotterNearby();
  Serial.printf("[today] restored %s: day %lu, %u overhead, set %s\n", got == sizeof(s) ? "ok" : "nothing",
                (unsigned long)spotterSummary().dayKey, spotterSummary().overhead, setLen ? "ok" : "none");
}

void todayUpdate(const Traffic &t, bool newPoll, bool trafficUp, uint32_t nowMs) {
  SpotterClock c;
  g_clockValid = clockNow(c);
  lock();
  spotterUpdate(t, newPoll, trafficUp, c, nowMs);
  unlock();
}

void todayService() {
  const uint32_t now = millis();
  char line[192], path[40];
  const bool sd = sdOk() && !g_sdDead;

  // 1. CSV lines: take one item at a time under the mutex, write it under sdLock.
  for (int budget = 0; budget < 8; budget++) {
    PassRec p;
    PromiseLine pl;
    DayLine dl;
    char type[5];
    bool haveP, havePl = false, haveDl = false, haveT = false;
    SpotterClock c;
    clockNow(c);
    lock();
    haveP = c.valid && spotterTakePassLine(p, c.epoch);   // no clock: no window math
    if (!haveP) havePl = spotterTakePromiseLine(pl);
    if (!haveP && !havePl) haveDl = spotterTakeDayLine(dl);
    if (!haveP && !havePl && !haveDl) haveT = spotterTakeTypeQuery(type);
    unlock();
    if (!(haveP || havePl || haveDl || haveT)) break;
    if (!sd) continue;                            // no card: the lines are simply not kept
    uint16_t sBefore = 0, dBefore = 0;
    bool ok = true, answered = false;
    sdLock(UINT32_MAX);
    if (haveP) {
      spotterCsvPass(p, hhmm, line, sizeof(line));
      logPath(path, sizeof(path), dayOf(p.peakEpoch), "");
      ok = append(path, SPOTTER_CSV_PASS_HEADER, line);
    } else if (havePl) {
      spotterCsvPromise(pl, hhmm, line, sizeof(line));
      logPath(path, sizeof(path), dayOf(pl.madeEpoch), "-promises");
      ok = append(path, SPOTTER_CSV_PROMISE_HEADER, line);
    } else if (haveDl) {
      spotterCsvDay(dl, line, sizeof(line));
      ok = append(TODAY_DIR "/days.csv", SPOTTER_CSV_DAY_HEADER, line);
    } else if (haveT && c.valid) {
      ok = answered = typeSighting(type, c.dayKey, sBefore, dBefore);
    }
    sdUnlock();
    lock();
    if (answered) spotterTypeAnswer(type, sBefore, dBefore, c.dayKey);
    if (!ok) spotterLineLost();                 // never silent
    unlock();
    // A card that fails at runtime (pulled, flaky) must not stall the net task on SdFat
    // timeouts every pass: after 3 failures in a row Today stops using it until reboot.
    g_sdFails = ok ? 0 : g_sdFails + 1;
    if (g_sdFails >= 3) {
      g_sdDead = true;
      lock();
      spotterSetLogging(false);
      unlock();
      Serial.println("[today] SD writes failing: log off until reboot");
      break;
    }
  }

  // 2. The nearby hash set, stamped with its day: every 5 min when changed, and right
  // after a rollover (SD only; without it, "~212").
  static uint32_t setDay = 0;
  uint32_t curDay;
  lock();
  curDay = spotterSummary().dayKey;
  unlock();
  if (sd && (now - g_lastSetMs > 300000UL || curDay != setDay)) {
    bool dirty;
    lock();
    dirty = spotterSetDirty(true);
    SetFile setf;                               // on the net stack (16 KB)
    if (dirty) {
      size_t n;
      setf.dayKey = spotterSummary().dayKey;
      memcpy(setf.bits, spotterSet(n), sizeof(setf.bits));
    }
    unlock();
    if (dirty) {
      sdLock(UINT32_MAX);
      File32 f = sdFs().open(TODAY_SET, O_RDWR | O_CREAT);   // overwrite in place: no truncate
      if (f) {                                                // window, no cluster churn
        f.seekSet(0);
        f.write(&setf, sizeof(setf));
        f.close();
      }
      sdUnlock();
    }
    g_lastSetMs = now;
    setDay = curDay;
  }

  // 3. The NVS summary (flash wear, and an erase stalls both cores): at most once a
  // minute for real changes (passes, promises, rarest), and every 10 min for the nearby
  // count alone.
  lock();
  const bool real = spotterPersistVersion() != g_savedVersion;
  const bool nearbyOnly = spotterNearby() != g_savedNearby;
  unlock();
  if ((real && now - g_lastNvsMs > 60000UL) || (nearbyOnly && now - g_lastNvsMs > 600000UL)) {
    TodaySummary s;                             // on the net stack
    lock();
    s = spotterSummary();
    g_savedVersion = spotterPersistVersion();
    g_savedNearby = spotterNearby();
    unlock();
    Preferences p;
    p.begin(NVS_NS, false);
    p.putBytes(NVS_KEY, &s, sizeof(s));
    p.end();
    g_lastNvsMs = now;
  }
}

uint32_t todayVersion() { return spotterVersion(); }

void todaySnapshot(TodaySummary &out, uint16_t &nearby) {
  lock();
  out = spotterSummary();
  nearby = spotterNearby();
  unlock();
}

uint16_t todayOverhead() {
  lock();
  const uint16_t n = spotterSummary().overhead;
  unlock();
  return n;
}

bool todayLastPass(PassRec &out) {
  lock();
  const bool ok = spotterLastPass(out);
  unlock();
  return ok;
}

bool todayClockValid() { return g_clockValid; }
