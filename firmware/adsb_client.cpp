#include "adsb_client.h"
#include "adsb_parse.h"
#include "http_json.h"

#include <algorithm>

namespace {

uint8_t  g_provider = 0;           // 0 = adsb.fi, 1 = adsb.lol
uint8_t  g_primaryFails = 0;
uint32_t g_fallbackSince = 0;

// Work buffer lives in .bss, not on the 12 KB net-task stack.
Aircraft g_buf[MAX_AIRCRAFT * 2];

bool fetchFrom(uint8_t provider, Traffic &t) {
  char url[128];
  uint8_t radius;
  uint16_t unusedInterval;
  appGetPollPlan(radius, unusedInterval);   // the map widens it (docs/08)
  snprintf(url, sizeof(url), provider == 0 ? ADSB_PRIMARY_URL : ADSB_FALLBACK_URL,
           (double)OBS_LAT, (double)OBS_LON, (int)radius);

  struct Ctx { int n; } ctx{0};
  bool parsed;
  const int code = httpGetStreamed(url, [](ByteSource &src, void *c) {
    auto *x = static_cast<Ctx *>(c);
    x->n = adsbParseStream(src, g_buf, sizeof(g_buf) / sizeof(g_buf[0]));
    return x->n >= 0;
  }, &ctx, parsed);
  if (code != 200) return false;
  if (!parsed) {
    Serial.printf("[adsb] parse failed (%s)\n", ctx.n == -1 ? "no aircraft array" : "stream broke off");
    return false;
  }
  const int n = ctx.n;

  char pinned[7];
  appGetPinnedHex(pinned);                    // the map's selected plane
  const size_t keep = adsbKeepNearest(g_buf, n, MAX_AIRCRAFT, pinned);
  appTrafficLock();                           // `t` is the shared copy (app_state.h)
  t.totalInRadius = n;
  t.n = keep;
  memcpy(t.ac, g_buf, t.n * sizeof(Aircraft));
  appTrafficUnlock();
  return true;
}

}  // namespace

uint8_t *adsbScratch(size_t &len) {
  len = sizeof(g_buf);
  return reinterpret_cast<uint8_t *>(g_buf);
}

bool adsbFetch(Traffic &t) {
  // After a spell on the fallback, try the primary again.
  if (g_provider == 1 && millis() - g_fallbackSince > ADSB_PRIMARY_RETRY_MS) {
    g_provider = 0;
    g_primaryFails = 0;
  }

  bool ok = fetchFrom(g_provider, t);
  if (!ok && g_provider == 0 && ++g_primaryFails >= ADSB_FAILOVER_AFTER) {
    Serial.println("[adsb] primary failing -> fallback adsb.lol");
    g_provider = 1;
    g_fallbackSince = millis();
    ok = fetchFrom(g_provider, t);
  }
  if (ok && g_provider == 0) g_primaryFails = 0;

  appTrafficLock();
  t.provider = g_provider;
  t.ok = ok;
  if (ok) {
    t.fetchedMs = millis();
    t.failStreak = 0;
  } else if (t.failStreak < 255) {
    t.failStreak++;
  }
  appTrafficUnlock();
  return ok;
}
