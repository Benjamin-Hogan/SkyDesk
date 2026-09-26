// Sky Trails recorder, device side (docs/13-sky-trails.md -> Step 1). Runs in the net task,
// right after an ADS-B parse and BEFORE the 40-aircraft screen cap. Appends one poll record to
// /skydesk/trails/YYYY-MM-DD.bin under sdLock. The format is trails_log.h (pure, host-tested).
#include "trails_store.h"

#include <time.h>

#include "config.h"
#include "observer.h"
#include "sd_store.h"
#include "trails_log.h"

namespace {

#define TRAILS_DIR "/skydesk/trails"

uint32_t g_day = 0;                            // the day of the file being appended
uint8_t  g_fails = 0;
bool     g_dead = false;                       // 3 failed writes in a row: stop until reboot

uint32_t dayKeyOf(const struct tm &t) {
  return (uint32_t)((t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday);
}

void pathFor(uint32_t day, char *out, size_t n) {
  snprintf(out, n, TRAILS_DIR "/%04lu-%02lu-%02lu.bin", (unsigned long)(day / 10000), (unsigned long)(day / 100 % 100),
           (unsigned long)(day % 100));
}

// Keep TRAILS_KEEP_DAYS: delete older day files (list first, delete after). Caller holds sdLock.
void prune(time_t now) {
  const time_t cut = now - (time_t)TRAILS_KEEP_DAYS * 86400;
  struct tm ct;
  localtime_r(&cut, &ct);
  char cutName[16];
  snprintf(cutName, sizeof(cutName), "%04d-%02d-%02d", ct.tm_year + 1900, ct.tm_mon + 1, ct.tm_mday);
  char old[8][20];
  uint8_t nOld = 0;
  File32 dir = sdFs().open(TRAILS_DIR, O_RDONLY);
  if (!dir) return;
  File32 f;
  while (nOld < 8 && f.openNext(&dir, O_RDONLY)) {
    char name[24];
    f.getName(name, sizeof(name));
    f.close();
    if (strlen(name) >= 10 && strncmp(name, cutName, 10) < 0) snprintf(old[nOld++], sizeof(old[0]), "%s", name);
  }
  dir.close();
  for (uint8_t i = 0; i < nOld; i++) {
    char p[48];
    snprintf(p, sizeof(p), TRAILS_DIR "/%s", old[i]);
    sdFs().remove(p);
  }
}

}  // namespace

void trailsRecord(const Aircraft *ac, uint8_t n) {
  if (g_dead || !sdOk() || !n) return;
  const time_t now = time(nullptr);
  if (now < 1700000000) return;                        // no clock, no day
  struct tm lt;
  localtime_r(&now, &lt);
  const uint32_t day = dayKeyOf(lt);
  const uint32_t sec = (uint32_t)(lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec);
  uint8_t g_rec[4 + 80 * TRAILS_POINT_BYTES];         // one poll, on the net stack (no static RAM)
  const size_t len = trailsEncodePoll(ac, n, sec, obsBuildLat(), obsBuildLon(), g_rec, sizeof(g_rec));
  if (!len) return;                                    // nothing honest to record this poll

  char path[48];
  pathFor(day, path, sizeof(path));
  bool ok = false;
  sdLock(UINT32_MAX);
  if (day != g_day) {                                  // a new day (or boot): the dir + retention
    sdFs().mkdir(TRAILS_DIR);
    prune(now);
    g_day = day;
  }
  File32 f = sdFs().open(path, O_WRONLY | O_CREAT | O_APPEND);
  if (f) {
    ok = true;
    if (f.fileSize() > 0 && f.fileSize() < TRAILS_HDR_BYTES) f.truncate(0);   // a header torn by a power cut
    if (f.fileSize() == 0) {
      uint8_t hdr[TRAILS_HDR_BYTES];
      trailsHeader(day, obsBuildLat(), obsBuildLon(), hdr);
      ok = f.write(hdr, sizeof(hdr)) == sizeof(hdr);
    }
    ok = ok && f.write(g_rec, len) == len;
    f.close();
  }
  sdUnlock();
  g_fails = ok ? 0 : g_fails + 1;
  if (g_fails >= 3) {
    g_dead = true;
    Serial.println("[trails] SD writes failing: recorder off until reboot");
  }
}
