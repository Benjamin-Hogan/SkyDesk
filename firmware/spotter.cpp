#include "spotter.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "aircraft_names.h"
#include "map_model.h"
#include "route_client.h"

// Rules: docs/11-today.md -> Definitions. Everything here is pure; the device glue
// (today_store.cpp) owns the mutex, SD and NVS.

static_assert(sizeof(TodaySummary) < 1024, "NVS blob: keep TodaySummary small");

namespace {

struct Promise {
  char hex[7];
  bool used, excluded;
  uint8_t provider;
  uint16_t leadS;
  uint32_t madeMs, dueMs, madeEpoch;
};

struct Judged {                                // first promise wins: no re-promise for a while
  char hex[7];
  uint32_t epoch;
};

SpotterLabelFn g_labels = nullptr;
TodaySummary   g_sum;
uint8_t        g_set[TODAY_SET_BITS / 8];
uint16_t       g_setBits = 0;
bool           g_setDirty = false;
PassRec        g_open[TODAY_OPEN];         // hex[0] == 0: free
PassRec        g_pend[TODAY_PEND];         // out of `recent`, not logged, still mergeable
Promise        g_prom[TODAY_PROMISES];
Judged         g_judged[TODAY_JUDGED];
uint8_t        g_judgedNext = 0;
PromiseLine    g_plog[TODAY_PLOG];
uint8_t        g_plogHead = 0, g_plogN = 0;
char           g_typeq[TODAY_TYPEQ][5];
uint8_t        g_typeqHead = 0, g_typeqN = 0;
DayLine        g_day;
bool           g_hasDay = false;
bool           g_logging = false;
uint32_t       g_lastMs = 0;               // outage accounting
uint32_t       g_outMs = 0;
uint8_t        g_outHour = 255;
uint32_t       g_version = 1;
uint32_t       g_persist = 1;

void copyStr(char *dst, size_t n, const char *src) {
  strncpy(dst, src ? src : "", n - 1);
  dst[n - 1] = '\0';
}

void changed(bool persist) {
  g_version++;
  if (persist) g_persist++;
}

uint32_t tolMs(uint16_t leadS) {
  const uint32_t pct = (uint32_t)leadS * 300;                  // 30 % of the lead, in ms
  return pct > TODAY_PROMISE_TOL_S * 1000UL ? pct : TODAY_PROMISE_TOL_S * 1000UL;
}

const Aircraft *inSnap(const Traffic &t, const char *hex) {
  for (uint8_t i = 0; i < t.n; i++)
    if (strcmp(t.ac[i].hex, hex) == 0) return &t.ac[i];
  return nullptr;
}

PassRec *openPass(const char *hex) {
  for (auto &p : g_open)
    if (p.hex[0] && strcmp(p.hex, hex) == 0) return &p;
  return nullptr;
}

Promise *livePromise(const char *hex) {
  for (auto &p : g_prom)
    if (p.used && strcmp(p.hex, hex) == 0) return &p;
  return nullptr;
}

bool recentlyJudged(const char *hex, uint32_t epoch) {
  for (const auto &j : g_judged)
    if (j.hex[0] && strcmp(j.hex, hex) == 0 && epoch - j.epoch < TODAY_MERGE_S) return true;
  return false;
}

bool mergeable(const PassRec &r, const char *hex, uint32_t epoch) {
  return r.hex[0] && strcmp(r.hex, hex) == 0 && !r.logged && r.closeEpoch && epoch - r.closeEpoch < TODAY_MERGE_S;
}

void follow(PassRec &p, const Aircraft &a, const SpotterClock &c, uint32_t nowMs) {
  p.seenMs = nowMs ? nowMs : 1;
  if (a.elDeg <= p.peakEl) return;
  p.peakEl = a.elDeg;
  p.peakNm = a.distNm;
  p.peakEpoch = c.epoch - (uint32_t)(a.seenPos > 0 ? a.seenPos : 0);
  p.toward = spotterOctant(a.track);
  if (g_labels) g_labels(a, p.lab);
}

void pushRecent(const PassRec &p) {
  if (g_sum.nRecent == TODAY_RECENT) {
    const PassRec &old = g_sum.recent[TODAY_RECENT - 1];
    if (!old.logged) {                        // still mergeable: park it until its window closes
      PassRec *slot = nullptr;
      for (auto &q : g_pend)
        if (!q.hex[0] && !slot) slot = &q;
      if (!slot) {                            // 6 passes inside 10 min: the oldest parked line is lost
        slot = &g_pend[0];
        for (auto &q : g_pend)
          if (q.closeEpoch < slot->closeEpoch) slot = &q;
        g_sum.dropped++;                      // reported, never silent
      }
      *slot = old;
    }
    g_sum.nRecent--;
  }
  memmove(&g_sum.recent[1], &g_sum.recent[0], g_sum.nRecent * sizeof(PassRec));
  g_sum.recent[0] = p;
  g_sum.nRecent++;
}

void resolve(Promise &p, PromiseOutcome o, int32_t errMs) {
  if (p.excluded) o = PromiseOutcome::Excluded;
  switch (o) {
    case PromiseOutcome::Kept: g_sum.today.kept++; break;
    case PromiseOutcome::Excluded: g_sum.today.excluded++; break;
    default: g_sum.today.broken++; break;
  }
  if (g_plogN == TODAY_PLOG) {
    g_plogHead = (g_plogHead + 1) % TODAY_PLOG;
    g_plogN--;
    g_sum.dropped++;
  }
  PromiseLine &l = g_plog[(g_plogHead + g_plogN++) % TODAY_PLOG];
  copyStr(l.hex, sizeof(l.hex), p.hex);
  l.outcome = o;
  l.leadS = p.leadS;
  l.errS = (int16_t)(errMs / 1000);
  l.madeEpoch = p.madeEpoch;
  Judged &j = g_judged[g_judgedNext++ % TODAY_JUDGED];   // its first promise has been judged
  copyStr(j.hex, sizeof(j.hex), p.hex);
  j.epoch = p.madeEpoch;
  p.used = false;
  changed(true);
}

void enter(const Traffic &t, const char *hex, const SpotterClock &c, uint32_t nowMs) {
  // Heads-up: judge the FIRST promise made for this hex (docs/11 -> Promise).
  if (Promise *p = livePromise(hex)) {
    const int32_t err = (int32_t)(t.fetchedMs - p->dueMs);
    const uint32_t tol = tolMs(p->leadS);
    const PromiseOutcome o = (uint32_t)(err < 0 ? -err : err) <= tol ? PromiseOutcome::Kept
                             : err < 0                               ? PromiseOutcome::Early
                                                                     : PromiseOutcome::Late;
    resolve(*p, o, err);
  }
  if (openPass(hex)) return;
  PassRec *slot = nullptr;
  for (auto &s : g_open)
    if (!s.hex[0] && !slot) slot = &s;
  if (!slot) return;                          // > TODAY_OPEN overhead at once: not counted anywhere
  // The same plane back within the merge window re-opens its pass (trainers circle).
  for (uint8_t i = 0; i < g_sum.nRecent; i++) {
    if (!mergeable(g_sum.recent[i], hex, c.epoch)) continue;
    *slot = g_sum.recent[i];
    memmove(&g_sum.recent[i], &g_sum.recent[i + 1], (g_sum.nRecent - i - 1) * sizeof(PassRec));
    g_sum.nRecent--;
    slot->closeEpoch = 0;
    slot->seenMs = nowMs ? nowMs : 1;
    changed(true);
    return;
  }
  for (auto &q : g_pend) {
    if (!mergeable(q, hex, c.epoch)) continue;
    *slot = q;
    q.hex[0] = '\0';
    slot->closeEpoch = 0;
    slot->seenMs = nowMs ? nowMs : 1;
    changed(true);
    return;
  }
  g_sum.overhead++;
  g_sum.hourly[c.hour < 24 ? c.hour : 23]++;
  memset(slot, 0, sizeof(*slot));
  copyStr(slot->hex, sizeof(slot->hex), hex);
  slot->peakEl = -90;
  slot->peakEpoch = c.epoch;
  slot->day = c.dayKey;
  slot->seenMs = nowMs ? nowMs : 1;
  if (const Aircraft *a = inSnap(t, hex)) follow(*slot, *a, c, nowMs);
  changed(true);
}

void leave(const char *hex, const SpotterClock &c) {
  PassRec *p = openPass(hex);
  if (!p) return;
  p->closeEpoch = c.epoch ? c.epoch : 1;
  pushRecent(*p);
  p->hex[0] = '\0';
  changed(true);
}

void rollover(const SpotterClock &c) {
  if (g_sum.dayKey) {
    g_day.dayKey = g_sum.dayKey;
    g_day.overhead = g_sum.overhead;
    g_day.nearby = spotterNearby();
    copyStr(g_day.rarest, sizeof(g_day.rarest), g_sum.rarest);
    g_day.kept = g_sum.today.kept;
    g_day.broken = g_sum.today.broken;
    g_day.excluded = g_sum.today.excluded;
    g_hasDay = true;
    // The ring is indexed by calendar day: after days off, the gap is empty days.
    int32_t gap = spotterDaysBetween(g_sum.dayKey, c.dayKey);
    if (gap < 1) gap = 1;
    for (int32_t k = 0; k < gap && k < TODAY_GATE_DAYS; k++) {
      memmove(&g_sum.ring[1], &g_sum.ring[0], (TODAY_GATE_DAYS - 1) * sizeof(PromiseDay));
      g_sum.ring[0] = k == 0 ? g_sum.today : PromiseDay{};
    }
    if (g_sum.loggedToday && g_sum.historyDays < 0xFFFF) g_sum.historyDays++;
  }
  g_sum.dayKey = c.dayKey;
  g_sum.overhead = 0;
  g_sum.nearbyBase = 0;
  g_sum.nearbySaved = 0;
  g_sum.nearbyLost = false;
  g_sum.loggedToday = g_logging;
  memset(g_sum.hourly, 0, sizeof(g_sum.hourly));
  g_sum.rarest[0] = '\0';
  g_sum.rarestSightings = 0;
  g_sum.today = {};
  g_sum.outageHours = 0;
  memset(g_set, 0, sizeof(g_set));
  g_setBits = 0;
  g_setDirty = true;
  g_typeqN = 0;                               // yesterday's lookups no longer name today's rarest
  changed(true);
}

void nearby(const Traffic &t) {
  bool grew = false;
  for (uint8_t i = 0; i < t.n; i++) {
    const Aircraft &a = t.ac[i];
    if (a.onGround || a.distNm > NEARBY_NM) continue;
    const uint16_t k = spotterSetHash(a.hex);
    if (g_set[k >> 3] & (1 << (k & 7))) continue;
    g_set[k >> 3] |= 1 << (k & 7);
    g_setBits++;
    grew = true;
    if (spotterJunkType(a.type, a.category)) continue;
    if (g_typeqN == TODAY_TYPEQ) {            // a burst (boot): drop, and say so
      g_sum.dropped++;
      continue;
    }
    copyStr(g_typeq[(g_typeqHead + g_typeqN++) % TODAY_TYPEQ], 5, a.type);
  }
  if (grew) {
    g_setDirty = true;
    g_sum.nearbySaved = spotterNearby();
    changed(false);                           // not worth an NVS write on its own (flash wear)
  }
}

void promises(const Traffic &t, const SpotterClock &c) {
  for (auto &p : g_prom) {
    if (!p.used) continue;
    if (t.failStreak > 0 || t.provider != p.provider) p.excluded = true;
    const int32_t late = (int32_t)(t.fetchedMs - (p.dueMs + tolMs(p.leadS)));
    if (late > 0) resolve(p, PromiseOutcome::Missed, 0);
  }
  if (!t.ok) return;
  for (uint8_t i = 0; i < t.n; i++) {
    const Aircraft &a = t.ac[i];
    if (a.onGround || a.seenPos > MAX_SEEN_POS_S || openPass(a.hex) || livePromise(a.hex)) continue;
    if (recentlyJudged(a.hex, c.epoch)) continue;        // the first promise was the one judged
    const uint8_t s = mapWillPopSecs(a);
    if (!s) continue;
    for (auto &p : g_prom) {
      if (p.used) continue;
      memset(&p, 0, sizeof(p));
      copyStr(p.hex, sizeof(p.hex), a.hex);
      p.used = true;
      p.excluded = t.failStreak > 0;
      p.provider = t.provider;
      p.leadS = s;
      p.madeMs = t.fetchedMs;
      p.dueMs = t.fetchedMs + s * 1000UL;
      p.madeEpoch = c.epoch;
      break;
    }
  }
}

bool passedOverhead(const char *icao) {
  for (uint8_t i = 0; i < g_sum.nRecent; i++)
    if (g_sum.recent[i].day == g_sum.dayKey && strcmp(g_sum.recent[i].lab.icao, icao) == 0) return true;
  for (const auto &p : g_open)
    if (p.hex[0] && strcmp(p.lab.icao, icao) == 0) return true;
  return false;
}

}  // namespace

void spotterInit(SpotterLabelFn labels) {
  g_labels = labels;
  PassEvent e;
  while (trackerTakePassEvent(e)) {}          // events from before the spotter existed
  memset(&g_sum, 0, sizeof(g_sum));
  g_sum.magic = TODAY_MAGIC;
  memset(g_set, 0, sizeof(g_set));
  memset(g_open, 0, sizeof(g_open));
  memset(g_pend, 0, sizeof(g_pend));
  memset(g_prom, 0, sizeof(g_prom));
  memset(g_judged, 0, sizeof(g_judged));
  g_setBits = 0;
  g_setDirty = g_hasDay = false;
  g_plogHead = g_plogN = g_typeqHead = g_typeqN = g_judgedNext = 0;
  g_lastMs = g_outMs = 0;
  g_outHour = 255;
  changed(true);
}

void spotterSetLogging(bool on) {
  g_logging = on;
  if (on && !g_sum.loggedToday && g_sum.dayKey) {
    g_sum.loggedToday = true;
    changed(true);
  }
}

void spotterUpdate(const Traffic &t, bool newPoll, bool trafficUp, const SpotterClock &c, uint32_t nowMs) {
  PassEvent e;
  if (!c.valid) {                             // no clock, no day: count nothing
    while (trackerTakePassEvent(e)) {}
    return;
  }
  if (g_sum.dayKey != c.dayKey) rollover(c);
  while (trackerTakePassEvent(e)) {
    if (e.enter) enter(t, e.hex, c, nowMs);
    else leave(e.hex, c);
  }
  // Outage: an hour with more than TODAY_OUTAGE_MIN of it is "not watching", not "quiet".
  if (g_outHour != c.hour) {
    g_outHour = c.hour;
    g_outMs = 0;
  } else if (!trafficUp && g_lastMs) {
    g_outMs += nowMs - g_lastMs;
    const uint32_t bit = 1UL << c.hour;
    if (g_outMs > TODAY_OUTAGE_MIN * 60000UL && !(g_sum.outageHours & bit)) {
      g_sum.outageHours |= bit;
      changed(true);
    }
  }
  g_lastMs = nowMs;
  if (!newPoll) return;
  for (auto &p : g_open) {
    if (!p.hex[0]) continue;
    if (const Aircraft *a = inSnap(t, p.hex)) follow(p, *a, c, nowMs);
    // Backstop: a Leave event can be lost (FIFO overflow); close what the feed has lost.
    else if (t.ok && nowMs - p.seenMs > (LOST_TIMEOUT_S + 10) * 1000UL) leave(p.hex, c);
  }
  if (t.ok) nearby(t);
  promises(t, c);
}

const TodaySummary &spotterSummary() { return g_sum; }
uint32_t spotterVersion() { return g_version; }
uint32_t spotterPersistVersion() { return g_persist; }

uint16_t spotterNearby() {
  const uint32_t n = (uint32_t)g_sum.nearbyBase + spotterLinearCount(g_setBits, TODAY_SET_BITS);
  return n > 0xFFFF ? 0xFFFF : (uint16_t)n;
}

bool spotterLastPass(PassRec &out) {
  if (!g_sum.nRecent) return false;
  out = g_sum.recent[0];
  return true;
}

bool spotterTakePassLine(PassRec &out, uint32_t nowEpoch) {
  // oldest first; only once the merge window has closed (a re-entry can't rewrite it)
  for (auto &q : g_pend) {
    if (!q.hex[0] || nowEpoch - q.closeEpoch < TODAY_MERGE_S) continue;
    out = q;
    out.logged = true;
    q.hex[0] = '\0';
    return true;
  }
  for (int i = g_sum.nRecent - 1; i >= 0; i--) {
    PassRec &r = g_sum.recent[i];
    if (r.logged || !r.closeEpoch || nowEpoch - r.closeEpoch < TODAY_MERGE_S) continue;
    r.logged = true;
    out = r;
    changed(true);                            // the NVS blob remembers it was logged
    return true;
  }
  return false;
}

bool spotterTakePromiseLine(PromiseLine &out) {
  if (!g_plogN) return false;
  out = g_plog[g_plogHead];
  g_plogHead = (g_plogHead + 1) % TODAY_PLOG;
  g_plogN--;
  return true;
}

bool spotterTakeTypeQuery(char out[5]) {
  if (!g_typeqN) return false;
  copyStr(out, 5, g_typeq[g_typeqHead]);
  g_typeqHead = (g_typeqHead + 1) % TODAY_TYPEQ;
  g_typeqN--;
  return true;
}

bool spotterTakeDayLine(DayLine &out) {
  if (!g_hasDay) return false;
  out = g_day;
  g_hasDay = false;
  return true;
}

void spotterLineLost() {
  g_sum.dropped++;
  changed(true);
}

void spotterTypeAnswer(const char *icao, uint16_t sightingsBefore, uint16_t daysBefore, uint32_t dayKey) {
  if (!icao || !icao[0] || dayKey != g_sum.dayKey || daysBefore >= TODAY_RARE_MAX_DAYS) return;
  if (g_sum.rarest[0]) {
    if (sightingsBefore > g_sum.rarestSightings) return;
    // a tie: a type that passed overhead today beats one that didn't; otherwise the newest wins
    if (sightingsBefore == g_sum.rarestSightings && passedOverhead(g_sum.rarest) && !passedOverhead(icao)) return;
  }
  copyStr(g_sum.rarest, sizeof(g_sum.rarest), icao);
  g_sum.rarestSightings = sightingsBefore;
  changed(true);
}

void spotterRestore(const TodaySummary &s, const uint8_t *set, size_t setLen) {
  if (s.magic != TODAY_MAGIC) return;
  g_sum = s;
  g_sum.nRecent = g_sum.nRecent > TODAY_RECENT ? TODAY_RECENT : g_sum.nRecent;
  g_setBits = 0;
  if (set && setLen == sizeof(g_set)) {
    memcpy(g_set, set, sizeof(g_set));
    for (uint8_t b : g_set)
      for (; b; b &= b - 1) g_setBits++;
  } else {
    memset(g_set, 0, sizeof(g_set));
    g_sum.nearbyBase = s.nearbySaved;                   // the saved estimate lives on ...
    g_sum.nearbyLost = s.nearbySaved > 0;               // ... but may now double count: "~212"
  }
  g_sum.nearbySaved = spotterNearby();
  changed(false);
}

const uint8_t *spotterSet(size_t &len) {
  len = sizeof(g_set);
  return g_set;
}

bool spotterSetDirty(bool clear) {
  const bool d = g_setDirty;
  if (clear) g_setDirty = false;
  return d;
}

int spotterGatePct(uint16_t minN, uint16_t *nOut, uint16_t *exOut) {
  uint32_t kept = g_sum.today.kept, broken = g_sum.today.broken, ex = g_sum.today.excluded;
  for (const auto &d : g_sum.ring) {
    kept += d.kept;
    broken += d.broken;
    ex += d.excluded;
  }
  const uint32_t n = kept + broken;
  if (nOut) *nOut = (uint16_t)(n > 0xFFFF ? 0xFFFF : n);
  if (exOut) *exOut = (uint16_t)(ex > 0xFFFF ? 0xFFFF : ex);
  if (n < minN || n == 0) return -1;
  return (int)((kept * 100 + n / 2) / n);
}

uint16_t spotterSetHash(const char *hex) {
  uint32_t h = 2166136261u;                             // FNV-1a over the id text
  for (const char *p = hex; *p; ++p) {
    h ^= (uint8_t)(*p | 0x20);                          // case-insensitive
    h *= 16777619u;
  }
  h *= 2654435761u;                                     // spread, then take the top bits
  return (uint16_t)(h >> (32 - 12)) & (TODAY_SET_BITS - 1);
}

uint16_t spotterLinearCount(uint16_t bitsSet, uint16_t bits) {
  if (!bitsSet) return 0;
  if (bitsSet >= bits) bitsSet = bits - 1;              // saturated: the best we can say
  const double m = bits;
  const double n = -m * log(1.0 - bitsSet / m);
  return n > 65535 ? 65535 : (uint16_t)(n + 0.5);
}

uint8_t spotterOctant(float trackDeg) {
  float t = fmodf(trackDeg, 360.0f);
  if (t < 0) t += 360.0f;
  return (uint8_t)((int)((t + 22.5f) / 45.0f) & 7);
}

int32_t spotterDaysBetween(uint32_t fromKey, uint32_t toKey) {
  auto days = [](uint32_t key) -> int32_t {             // days from civil (H. Hinnant)
    int32_t y = (int32_t)(key / 10000), m = (int32_t)(key / 100 % 100), d = (int32_t)(key % 100);
    y -= m <= 2;
    const int32_t era = (y >= 0 ? y : y - 399) / 400;
    const int32_t yoe = y - era * 400;
    const int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
  };
  return days(toKey) - days(fromKey);
}

// ---- words and CSV -------------------------------------------------------------------

bool spotterJunkType(const char *icao, const char *category) {
  if (!icao || !icao[0]) return true;
  if (category && (category[0] == 'C' || category[0] == 'c')) return true;   // C1-C7: ground / obstacles
  for (const char *j : {"TWR", "GND", "GRND"})
    if (strcmp(icao, j) == 0) return true;
  return false;
}

void spotterLabels(const Aircraft &a, const RouteInfo *route, PassLabel &out) {
  memset(&out, 0, sizeof(out));
  PlaneLabels pl;
  planeLabels(a, route, pl);                     // the TYPE words (the card's)
  copyStr(out.type, sizeof(out.type), pl.type);
  // The operator is persisted (CSV, NVS), so it comes ONLY from adsbdb's flight-route
  // airline or the built-in airline table - never registered_owner / ownOp, which name
  // private owners for Part 91 jets on airline-like callsigns (review: privacy).
  if (route && route->hasRoute && route->airline[0]) shortOperator(route->airline, out.op, sizeof(out.op));
  else airlineByCallsign(a.callsign, out.op, sizeof(out.op));
  copyStr(out.icao, sizeof(out.icao), a.type[0] ? a.type : (route ? route->icaoType : ""));
  copyStr(out.reg, sizeof(out.reg), a.reg);
  copyStr(out.callsign, sizeof(out.callsign), a.callsign);
  if (route && route->hasRoute && routePlausible(*route, a)) {
    copyStr(out.orig, sizeof(out.orig), route->origIata);
    copyStr(out.dest, sizeof(out.dest), route->destIata);
  }
  // The short form uses only codes people know (round 2: "YX ERJ-175" is noise).
  static const char *KNOWN[] = {"AA", "AS", "B6", "BA", "DL", "F9", "NK", "UA", "WN", "AC", "WS", "AF",
                                "KL", "LH", "HA", "G4", "SY", "AM", "Y4", "EK", "QR", "VS", "JL", "NH"};
  if (out.op[0] && route && route->flightIata[0] && route->flightIata[1]) {
    for (const char *k : KNOWN)
      if (route->flightIata[0] == k[0] && route->flightIata[1] == k[1]) {
        out.code[0] = k[0];
        out.code[1] = k[1];
        out.code[2] = '\0';
      }
  }
}

namespace {
// Append a CSV field: ASCII only, commas / quotes / control chars become spaces.
void field(char *buf, size_t n, size_t &len, const char *s, bool comma = true) {
  if (comma && len + 1 < n) buf[len++] = ',';
  for (const char *p = s ? s : ""; *p && len + 1 < n; ++p) {
    const unsigned char ch = (unsigned char)*p;
    buf[len++] = (ch < 32 || ch > 126 || ch == ',' || ch == '"') ? ' ' : (char)ch;
  }
  buf[len] = '\0';
}
}  // namespace

size_t spotterCsvPass(const PassRec &p, SpotterTimeFn fmt, char *buf, size_t n) {
  static const char *OCTANT[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  char t[6] = "", num[16];
  if (fmt) fmt(p.peakEpoch, t);
  size_t len = 0;
  buf[0] = '\0';
  field(buf, n, len, t, false);
  field(buf, n, len, p.hex);
  field(buf, n, len, p.lab.callsign);
  field(buf, n, len, p.lab.reg);
  field(buf, n, len, p.lab.icao);
  field(buf, n, len, p.lab.op);
  field(buf, n, len, p.lab.orig);
  field(buf, n, len, p.lab.dest);
  snprintf(num, sizeof(num), "%.0f", (double)p.peakEl);
  field(buf, n, len, num);
  snprintf(num, sizeof(num), "%.1f", (double)(p.peakNm * 1.150779f));
  field(buf, n, len, num);
  field(buf, n, len, OCTANT[p.toward & 7]);
  return len;
}

size_t spotterCsvPromise(const PromiseLine &l, SpotterTimeFn fmt, char *buf, size_t n) {
  static const char *OUTCOME[] = {"kept", "early", "late", "missed", "excluded"};
  char t[6] = "";
  if (fmt) fmt(l.madeEpoch, t);
  return (size_t)snprintf(buf, n, "%s,%s,%u,%s,%d", t, l.hex, (unsigned)l.leadS, OUTCOME[(int)l.outcome], (int)l.errS);
}

size_t spotterCsvDay(const DayLine &d, char *buf, size_t n) {
  return (size_t)snprintf(buf, n, "%04lu-%02lu-%02lu,%u,%u,%s,%u,%u,%u", (unsigned long)(d.dayKey / 10000),
                          (unsigned long)(d.dayKey / 100 % 100), (unsigned long)(d.dayKey % 100), (unsigned)d.overhead,
                          (unsigned)d.nearby, d.rarest, (unsigned)d.kept, (unsigned)d.broken, (unsigned)d.excluded);
}
