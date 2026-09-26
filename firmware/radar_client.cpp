#include "radar_client.h"
#include "observer.h"
#include <esp_heap_caps.h>
#include "app_state.h"
#include "config.h"
#include "adsb_client.h"
#include "http_json.h"
#include "sd_store.h"

#include <math.h>
#include <new>
#include <time.h>

// Rain radar frames (docs/10-rain-radar.md -> Data). Net task (core 0) only.
// Rules that keep the screen honest (v3-R1-1): exact n0q colours only, a frame with
// > 0.5 % misses is rejected; an empty frame right after a wet one is re-fetched once;
// the national quorum is published for the UI's "partial coverage" warning.

namespace {

RadarStatus g_st{};
uint32_t g_nextTry = 0;          // unix time before which nothing is attempted (backoff/retry)
uint32_t g_lastCheck = 0;        // last successful n0q_0.json check
bool     g_wasOpen = false;
uint32_t g_retriedValid = 0;     // the empty-after-wet frame already re-fetched once
char     g_lastWhy[20] = "";     // reason of the last Conv::Failed (shown on the radar screen)

// Frames leave the loop set as new ones arrive, but a file is only deleted after the UI
// has acknowledged a frame list without it: the UI may have it open (v3-R1-10).
uint32_t g_doomed[8];
uint8_t  g_nDoomed = 0;
uint32_t g_doomVersion = 0;

// Convert buffers: heap-allocated only while a frame is converted (TLS is closed by
// then), never permanent - RAM is what TLS runs out of (v3 on-device fix).
struct Work {
  union {                                // the level pass is done before cleaning starts
    struct {
      uint8_t plane[4][RADAR_W];
      uint8_t io[RADAR_W * 4];
      uint8_t head[2048];
    } a;
    uint8_t runs[RADAR_CLEAN_RUN_BYTES];
  } u;
  uint8_t row[RADAR_W];
  uint8_t packed[RADAR_W / 2];
};

// Publish only when something changed: every publish makes the UI re-read headers and
// redraw, and radarService runs every net pass (~20x a second) (reviewer H4).
RadarStatus g_published{};
uint32_t g_pubVersion = 0;
uint32_t publish() {
  RadarStatus a = g_st, b = g_published;
  a.version = b.version = 0;
  if (g_pubVersion && memcmp(&a, &b, sizeof(a)) == 0) return g_pubVersion;
  g_published = g_st;
  g_pubVersion = appSetRadar(g_st);
  return g_pubVersion;
}

// ---- time helpers (UTC; newlib here has no timegm) ---------------------------
int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + doe - 719468;
}

uint32_t parseIsoUtc(const char *s) {        // "2026-09-25T05:55:00Z"
  int Y, M, D, h, m, sec;
  if (!s || sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &sec) != 6) return 0;
  return (uint32_t)(daysFromCivil(Y, M, D) * 86400 + h * 3600 + m * 60 + sec);
}

void isoUtc(uint32_t t, char *out, size_t n) {
  const time_t tt = t;
  struct tm g;
  gmtime_r(&tt, &g);
  strftime(out, n, "%Y-%m-%dT%H:%M:%SZ", &g);
}

// ---- frame index ------------------------------------------------------------
bool haveFrame(uint32_t v) {
  for (uint8_t i = 0; i < g_st.n; i++)
    if (g_st.valid[i] == v) return true;
  return false;
}

bool readHdr(uint32_t v, RadarFrameHdr &h) {
  char p[40];
  radarFramePath(v, p, sizeof(p));
  File32 f = sdFs().open(p, O_RDONLY);
  if (!f) return false;
  const bool ok = f.read((uint8_t *)&h, sizeof(h)) == sizeof(h) && h.magic == RADAR_MAGIC && h.valid == v;
  f.close();
  return ok;
}

void wordsFromNewest() {
  g_st.rain = g_st.heavy = g_st.echo = false;
  if (!g_st.n) return;
  RadarFrameHdr h;
  if (!readHdr(g_st.valid[g_st.n - 1], h)) {           // never let a read error say "No rain"
    g_st.unreadable = true;
    return;
  }
  g_st.rain = h.rain;
  g_st.heavy = h.heavy;
  g_st.echo = h.echo;
  g_st.rainMi = h.rainMi;
  g_st.rainAz = h.rainAz;
  g_st.heavyMi = h.heavyMi;
  g_st.heavyAz = h.heavyAz;
}

void doom(uint32_t v) {
  for (uint8_t i = 0; i < g_nDoomed; i++)
    if (g_doomed[i] == v) return;
  if (g_nDoomed < sizeof(g_doomed) / sizeof(g_doomed[0])) g_doomed[g_nDoomed++] = v;
}

void purgeDoomed() {
  if (!g_nDoomed || appRadarAcked() < g_doomVersion) return;
  for (uint8_t i = 0; i < g_nDoomed; i++) {
    if (haveFrame(g_doomed[i])) continue;                // back in the loop set: keep it
    char p[40];
    radarFramePath(g_doomed[i], p, sizeof(p));
    sdFs().remove(p);
  }
  g_nDoomed = 0;
}

// Keep the loop set of the newest frame T (radarLoopSet: T + the fixed 10-min grid below it);
// frames outside it are doomed (deleted after the UI's ack). Temp files are removed at once.
void rescan() {
  g_nDoomed = 0;                                         // rebuilt from scratch (reviewer M5)
  uint32_t found[64];
  uint8_t nf = 0;
  // list first, delete after: never remove entries from the directory being iterated
  char junk[8][40];
  uint8_t nj = 0;
  File32 dir = sdFs().open(RADAR_DIR, O_RDONLY);
  if (dir) {
    File32 f;
    while (f.openNext(&dir, O_RDONLY)) {
      char name[40];
      f.getName(name, sizeof(name));
      f.close();
      const size_t L = strlen(name);
      const bool bin = L > 4 && strcmp(name + L - 4, ".bin") == 0;
      const uint32_t v = bin ? strtoul(name, nullptr, 10) : 0;
      if (v > 1600000000u && nf < 64) found[nf++] = v;
      else if (nj < 8) snprintf(junk[nj++], sizeof(junk[0]), RADAR_DIR "/%s", name);   // temp / junk
    }
    dir.close();
  }
  for (uint8_t i = 0; i < nj; i++) sdFs().remove(junk[i]);
  uint32_t newest = 0;
  for (uint8_t i = 0; i < nf; i++) newest = max(newest, found[i]);
  g_st.n = 0;
  uint32_t set[RADAR_FRAMES];
  const uint8_t ns = radarLoopSet(newest, set, RADAR_FRAMES, RADAR_FRAME_STEP_S);
  for (uint8_t k = 0; k < ns; k++)                        // oldest first
    for (uint8_t i = 0; i < nf; i++)
      if (found[i] == set[k]) g_st.valid[g_st.n++] = set[k];
  for (uint8_t i = 0; i < nf; i++)
    if (!haveFrame(found[i])) doom(found[i]);
  wordsFromNewest();
}

// ---- download + convert -----------------------------------------------------
bool sinkToFile(const uint8_t *buf, size_t n, void *ctx) {
  sdLock(UINT32_MAX);                                    // per chunk: the UI can draw in between
  const bool ok = static_cast<File32 *>(ctx)->write(buf, n) == n;
  sdUnlock();
  return ok;
}

// The convert is several seconds of core-0 work (4.7 s in V2; slower since the SD card
// runs in SHARED_SPI mode). Without a yield, IDLE0 starves and the task watchdog resets
// the board (3.0 field log). 1 ms every 16 rows, holding sdLock is fine (the UI uses
// sdLock(0) and skips a frame).
void yieldSometimes() {
  static uint8_t n = 0;
  if (++n % 16 == 0) vTaskDelay(1);
}

class FileRows : public RadarRows {
 public:
  explicit FileRows(File32 &f) : f_(f) {}
  bool read(uint16_t y, uint8_t *row) override {
    yieldSometimes();
    return f_.seekSet((uint32_t)y * RADAR_W) && f_.read(row, RADAR_W) == RADAR_W;
  }
  bool write(uint16_t y, const uint8_t *row) override {
    yieldSometimes();
    return f_.seekSet((uint32_t)y * RADAR_W) && f_.write(row, RADAR_W) == RADAR_W;
  }

 private:
  File32 &f_;
};

enum class Conv : uint8_t { Ok, Unreadable, EmptyAfterWet, Failed };

Conv convertWith(Work &w, uint32_t valid) {
  File32 raw = sdFs().open(RADAR_DIR "/raw.tif", O_RDONLY);
  if (!raw) {
    snprintf(g_lastWhy, sizeof(g_lastWhy), "sd read");
    return Conv::Failed;
  }
  const size_t hn = raw.read(w.u.a.head, sizeof(w.u.a.head));
  TiffInfo ti;
  if (!tiffParse(w.u.a.head, hn, ti) || ti.w != RADAR_W || ti.h != RADAR_H) {
    Serial.println("[radar] unexpected TIFF layout");     // e.g. a WMS XML error body
    raw.close();
    snprintf(g_lastWhy, sizeof(g_lastWhy), "bad tiff");
    return Conv::Unreadable;
  }
  if (raw.fileSize() < tiffDataEnd(ti)) {                    // no Content-Length: prove completeness
    snprintf(g_lastWhy, sizeof(g_lastWhy), "cut %uk/%uk", (unsigned)(raw.fileSize() / 1024),
             (unsigned)(tiffDataEnd(ti) / 1024));
    raw.close();
    return Conv::Failed;
  }
  // 1. levels, one byte per pixel; count echo pixels and colour misses; clutter mask
  uint32_t echo = 0, misses = 0;
  File32 lv = sdFs().open(RADAR_DIR "/lv.tmp", O_WRONLY | O_CREAT | O_TRUNC);
  bool ok = (bool)lv;
  for (uint16_t y = 0; ok && y < RADAR_H; y++) {
    yieldSometimes();
    for (uint8_t p = 0; ok && p < 4; p++) {
      uint8_t step;
      const uint32_t off = tiffRowOffset(ti, p, y, step);
      if (step == 1) {
        ok = raw.seekSet(off) && raw.read(w.u.a.plane[p], RADAR_W) == RADAR_W;
      } else {                                               // chunky RGBA: one read, de-interleave
        if (p == 0) ok = raw.seekSet(off) && raw.read(w.u.a.io, RADAR_W * 4) == RADAR_W * 4;
        for (uint16_t x = 0; ok && x < RADAR_W; x++) w.u.a.plane[p][x] = w.u.a.io[x * 4 + p];
      }
    }
    for (uint16_t x = 0; ok && x < RADAR_W; x++) {
      bool miss;
      const uint8_t a = w.u.a.plane[3][x];
      w.row[x] = radarLevel(w.u.a.plane[0][x], w.u.a.plane[1][x], w.u.a.plane[2][x], a, &miss);
      echo += a != 0;
      misses += miss;
    }
    if (ok) ok = lv.write(w.row, RADAR_W) == RADAR_W;
  }
  raw.close();
  if (lv) lv.close();
  sdFs().remove(RADAR_DIR "/raw.tif");
  if (!ok) return Conv::Failed;
  if (misses * 1000 > (uint32_t)RADAR_MISS_PERMILLE * max<uint32_t>(echo, 1)) {
    Serial.printf("[radar] frame rejected: %u of %u echo pixels are not n0q colours\n", misses, echo);
    sdFs().remove(RADAR_DIR "/lv.tmp");
    snprintf(g_lastWhy, sizeof(g_lastWhy), "bad colors");
    return Conv::Unreadable;
  }

  // 3. clean clutter, words, wet mask, pack
  lv = sdFs().open(RADAR_DIR "/lv.tmp", O_RDWR);
  if (!lv) return Conv::Failed;
  FileRows rows(lv);
  size_t scratchLen;
  uint8_t *scratch = adsbScratch(scratchLen);            // idle between ADS-B fetches (net task)
  const int rc = radarClean(rows, RADAR_W, RADAR_H, RADAR_MIN_BLOB_PX, radarNearClutter, scratch, scratchLen,
                            w.u.runs, sizeof(w.u.runs));
  if (rc == RADAR_CLEAN_NOMEM) {                         // never publish an uncleaned frame (M2)
    lv.close();
    sdFs().remove(RADAR_DIR "/lv.tmp");
    snprintf(g_lastWhy, sizeof(g_lastWhy), "ram %uk", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024);
    return Conv::Failed;
  }
  if (rc == RADAR_CLEAN_OVERFLOW) Serial.println("[radar] too many rain runs to clean - frame kept as is");
  RadarFrameHdr h{};
  h.magic = RADAR_MAGIC;
  h.valid = valid;
  // words name only significant blobs (radarClean flags the rest); the weather cue uses
  // the same rule, so the strip and the cue always talk about the same rain
  h.rain = radarNearest(rows, RADAR_W, RADAR_VIS_H, 1, h.rainMi, h.rainAz);
  h.heavy = radarNearest(rows, RADAR_W, RADAR_VIS_H, 3, h.heavyMi, h.heavyAz);
  char tmp[40], fin[40];
  snprintf(tmp, sizeof(tmp), RADAR_DIR "/%lu.tmp", (unsigned long)valid);
  radarFramePath(valid, fin, sizeof(fin));
  File32 out = sdFs().open(tmp, O_WRONLY | O_CREAT | O_TRUNC);
  ok = (bool)out && out.write((const uint8_t *)&h, sizeof(h)) == sizeof(h);   // patched below
  for (uint16_t y = 0; ok && y < RADAR_H; y++) {
    ok = rows.read(y, w.row);
    for (uint16_t x = 0; ok && x < RADAR_W; x += 2) {
      const uint8_t l0 = RADAR_LV(w.row[x]), l1 = RADAR_LV(w.row[x + 1]);
      w.packed[x / 2] = (uint8_t)((l0 << 4) | l1);
      if (l0 || l1) {
        h.echo = 1;
        const uint16_t cell = (y / 4) * RADAR_WET_W + x / 4;
        h.wet[cell >> 3] |= (uint8_t)(1 << (cell & 7));
      }
    }
    if (ok) ok = out.write(w.packed, sizeof(w.packed)) == sizeof(w.packed);
  }
  if (ok) ok = out.seekSet(0) && out.write((const uint8_t *)&h, sizeof(h)) == sizeof(h);
  lv.close();
  if (out) out.close();
  sdFs().remove(RADAR_DIR "/lv.tmp");
  if (!ok) {
    sdFs().remove(tmp);
    snprintf(g_lastWhy, sizeof(g_lastWhy), "sd write");
    return Conv::Failed;
  }
  // an empty frame (nothing DRAWN after cleaning) right after a wet one is suspect:
  // re-fetch it once first (reviewer M3: the raw alpha count includes sub-20 dBZ returns)
  if (!h.echo && g_st.n && g_st.echo && (int32_t)(valid - g_st.valid[g_st.n - 1]) < 20 * 60 &&
      g_retriedValid != valid) {
    g_retriedValid = valid;
    sdFs().remove(tmp);
    Serial.println("[radar] empty frame right after rain - re-fetching once");
    return Conv::EmptyAfterWet;
  }
  sdFs().remove(fin);
  return sdFs().rename(tmp, fin) ? Conv::Ok : Conv::Failed;   // the UI never sees a half-written frame
}

Conv convert(uint32_t valid) {
  Work *w = new (std::nothrow) Work;
  if (!w) {
    Serial.printf("[radar] no RAM to convert (largest block %u)\n", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    snprintf(g_lastWhy, sizeof(g_lastWhy), "ram %uk", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024);
    return Conv::Failed;
  }
  sdLock(UINT32_MAX);                                    // both file slots: keep the UI out
  const Conv c = convertWith(*w, valid);
  sdUnlock();
  delete w;
  return c;
}

Conv fetchFrame(uint32_t valid) {
  // The frame must line up with the radar basemap and clutter mask: the BUILD location
  // (radar only runs inside SETUP_RADAR_GATE_MI of it, docs/12).
  const double cLat = obsBuildLat(), cLon = obsBuildLon();
  const double coslat = cos(cLat * DEG_TO_RAD);
  const double k = RADAR_PPN * 60.0;
  const double west = cLon - RADAR_CX / (k * coslat), east = cLon + (RADAR_W - RADAR_CX) / (k * coslat);
  const double north = cLat + RADAR_CY / k, south = cLat - (RADAR_H - RADAR_CY) / k;
  char iso[24], url[420];
  isoUtc(valid, iso, sizeof(iso));
  snprintf(url, sizeof(url),
           "https://mesonet.agron.iastate.edu/cgi-bin/wms/nexrad/n0q-t.cgi?SERVICE=WMS&VERSION=1.1.1"
           "&REQUEST=GetMap&LAYERS=nexrad-n0q-wmst&STYLES=&SRS=EPSG:4326&BBOX=%.4f,%.4f,%.4f,%.4f"
           "&WIDTH=%d&HEIGHT=%d&FORMAT=image/tiff&TRANSPARENT=true&TIME=%s",
           west, south, east, north, RADAR_W, RADAR_H, iso);
  const uint32_t t0 = millis();
  sdLock(UINT32_MAX);
  File32 f = sdFs().open(RADAR_DIR "/raw.tif", O_WRONLY | O_CREAT | O_TRUNC);
  sdUnlock();
  if (!f) {
    snprintf(g_lastWhy, sizeof(g_lastWhy), "sd write");
    return Conv::Failed;
  }
  size_t got, expected;
  // 15 s: IEM renders a WMS frame on request; a busy moment can exceed the 7 s default.
  const int code = httpGetBody(url, sinkToFile, &f, 400000, got, expected, 15000);
  sdLock(UINT32_MAX);
  f.close();
  sdUnlock();
  if (code != 200 || !got || (expected && got != expected)) {
    Serial.printf("[radar] frame %s failed (http %d, %u/%u bytes)\n", iso, code, (unsigned)got, (unsigned)expected);
    if (code != 200) snprintf(g_lastWhy, sizeof(g_lastWhy), "http %d", code);
    else snprintf(g_lastWhy, sizeof(g_lastWhy), "cut %uk/%uk", (unsigned)(got / 1024), (unsigned)(expected / 1024));
    sdLock(UINT32_MAX);
    sdFs().remove(RADAR_DIR "/raw.tif");
    sdUnlock();
    return Conv::Failed;
  }
  const uint32_t t1 = millis();
  snprintf(g_lastWhy, sizeof(g_lastWhy), "convert");
  const Conv c = convert(valid);
  Serial.printf("[radar] frame %s: %s, %u B in %u ms, convert %u ms, heap min %u\n", iso,
                c == Conv::Ok ? "ok" : c == Conv::Unreadable ? "UNREADABLE" : c == Conv::EmptyAfterWet ? "suspect" : "FAILED",
                (unsigned)got, (unsigned)(t1 - t0), (unsigned)(millis() - t1), heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
  return c;
}

bool checkNewest() {
  JsonDocument filter;
  filter["meta"]["valid"] = true;
  filter["meta"]["radar_quorum"] = true;
  JsonDocument doc;
  bool jsonOk;
  const int code = httpGetJson("https://mesonet.agron.iastate.edu/data/gis/images/4326/USCOMP/n0q_0.json",
                               doc, filter, jsonOk);
  if (code != 200 || !jsonOk) return false;
  const uint32_t v = parseIsoUtc(doc["meta"]["valid"] | "");
  if (!v) return false;
  unsigned have = 0, all = 0;
  const char *q = doc["meta"]["radar_quorum"] | "";
  g_st.quorumPct = sscanf(q, "%u/%u", &have, &all) == 2 && all ? (uint8_t)min<unsigned>(100, have * 100 / all) : 100;
  if (v != g_st.newestValid) g_st.unreadable = false;          // a new frame gets a fresh chance
  g_st.newestValid = v;
  return true;
}

void fail(uint32_t now, const char *why) {
  snprintf(g_st.err, sizeof(g_st.err), "%s", why);
  Serial.printf("[radar] attempt failed: %s\n", why);
  g_st.failStreak = min<uint8_t>(g_st.failStreak + 1, 5);
  g_st.lastFailed = true;
  g_nextTry = now + min<uint32_t>(RADAR_POLL_DRY_S, 60u << g_st.failStreak);
}

}  // namespace

void radarFramePath(uint32_t valid, char *out, size_t n) {
  snprintf(out, n, RADAR_DIR "/%lu.bin", (unsigned long)valid);
}

void radarClientInit() {
  memset(&g_st, 0, sizeof(g_st));
  g_st.sdOk = sdOk();
  g_st.quorumPct = 100;
  if (g_st.sdOk) {
    sdLock(UINT32_MAX);
    sdFs().mkdir(RADAR_DIR);
    rescan();
    sdUnlock();
    Serial.printf("[radar] %u frame(s) on SD\n", g_st.n);
  }
  g_doomVersion = publish();
}

bool radarService(bool screenOpen, bool wetHint) {
  if (!g_st.sdOk || !obsInRadarGate()) return false;    // docs/12: frames only fit the build location
  if (g_nDoomed && appRadarAcked() >= g_doomVersion) {
    sdLock(UINT32_MAX);
    purgeDoomed();
    sdUnlock();
  }
  const uint32_t now = (uint32_t)time(nullptr);
  if (now < 1700000000u) return false;                   // no clock yet
  if (screenOpen && !g_wasOpen) g_lastCheck = 0;       // opened: check (and fetch) at once
                                                         // - but never through a backoff
  g_wasOpen = screenOpen;
  if ((int32_t)(now - g_nextTry) < 0) return false;      // backing off / waiting to re-fetch

  const uint32_t every = screenOpen ? RADAR_POLL_OPEN_S : wetHint ? RADAR_POLL_WET_S : RADAR_POLL_DRY_S;
  if (!g_lastCheck || now - g_lastCheck >= every) {
    if (!checkNewest()) {
      int code, tls;
      httpLastFailure(code, tls);
      char why[20];
      snprintf(why, sizeof(why), "json %d", code);
      fail(now, why);
      publish();
      return false;
    }
    g_lastCheck = g_st.checkedEpoch = now;
    if (haveFrame(g_st.newestValid)) {                   // nothing failed: clear the old failure
      g_st.failStreak = 0;
      g_st.lastFailed = false;
      g_st.err[0] = 0;
    }
  }
  if (!g_st.newestValid) return false;

  // What to fetch: the newest frame always (it feeds the weather cue). The rest of the
  // loop only while the radar screen is up AND the newest frame has rain (a dry sky
  // is one frame, no loop), newest first, one per call.
  uint32_t want = 0;
  if (!haveFrame(g_st.newestValid) && !g_st.unreadable) {
    want = g_st.newestValid;
  } else if (screenOpen && g_st.echo) {                 // backfill, newest first, on the fixed grid
    uint32_t set[RADAR_FRAMES];
    const uint8_t ns = radarLoopSet(g_st.newestValid, set, RADAR_FRAMES, RADAR_FRAME_STEP_S);
    for (int k = ns - 2; k >= 0 && !want; k--)
      if (!haveFrame(set[k])) want = set[k];
  }
  g_st.loading = want && want != g_st.newestValid;
  if (!want) {
    publish();
    return false;
  }
  publish();
  const Conv c = fetchFrame(want);                     // locks the card itself, piecewise
  if (c == Conv::Ok) {
    g_st.failStreak = 0;
    g_st.lastFailed = false;
    g_st.err[0] = 0;
  } else if (c == Conv::EmptyAfterWet) {
    g_nextTry = now + 60;                                // one re-fetch, a minute later
  } else if (c == Conv::Unreadable) {
    if (want == g_st.newestValid) g_st.unreadable = true;  // keep the previous frame up
    fail(now, g_lastWhy);
  } else {
    fail(now, g_lastWhy);
  }
  sdLock(UINT32_MAX);
  rescan();
  sdUnlock();
  g_st.loading = false;
  if (screenOpen && g_st.echo) {
    uint32_t set[RADAR_FRAMES];
    const uint8_t ns = radarLoopSet(g_st.newestValid, set, RADAR_FRAMES, RADAR_FRAME_STEP_S);
    for (uint8_t k = 0; k < ns; k++) g_st.loading = g_st.loading || !haveFrame(set[k]);
  }
  const uint32_t v = publish();
  if (g_nDoomed) g_doomVersion = v;
  return true;
}
