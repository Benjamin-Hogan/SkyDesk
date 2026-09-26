// Host tests for radar_model.cpp (docs/10-rain-radar.md): TIFF parsing on a REAL
// IEM frame, n0q colour -> level, clutter cleaning, nearest rain.
#include "radar_model.h"
#include <cstdio>
#include <math.h>
#include <string.h>
#include <string>
#include <vector>

extern int g_fail;
#define CHECK(cond, msg)                                              \
  do {                                                                \
    if (!(cond)) { std::printf("  FAIL: %s (line %d)\n", msg, __LINE__); g_fail++; } \
    else std::printf("  ok:   %s\n", msg);                            \
  } while (0)

namespace {
std::vector<uint8_t> slurpBin(const char *name) {
  std::string p = std::string(FIXTURES) + "/" + name;
  FILE *f = std::fopen(p.c_str(), "rb");
  std::vector<uint8_t> v;
  if (!f) return v;
  uint8_t buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) v.insert(v.end(), buf, buf + n);
  std::fclose(f);
  return v;
}

struct MemRows : RadarRows {
  uint16_t w, h;
  std::vector<uint8_t> px;
  MemRows(uint16_t w_, uint16_t h_) : w(w_), h(h_), px(w_ * h_, 0) {}
  bool read(uint16_t y, uint8_t *row) override { memcpy(row, &px[y * w], w); return true; }
  bool write(uint16_t y, const uint8_t *row) override { memcpy(&px[y * w], row, w); return true; }
  uint8_t &at(int x, int y) { return px[y * w + x]; }
  int count() const { int c = 0; for (uint8_t v : px) c += v != 0; return c; }
};
}  // namespace

void testRadar() {
  // ---- TIFF on a real frame (14 Jul 2024 monsoon, 64x48, the 50 mi view) ----
  const std::vector<uint8_t> tif = slurpBin("iem_n0q_64x48.tif");
  TiffInfo ti;
  CHECK(!tif.empty() && tiffParse(tif.data(), tif.size() < 2048 ? tif.size() : 2048, ti),
        "real IEM TIFF header parses from its first 2 KB");
  CHECK(ti.w == 64 && ti.h == 48 && ti.planar, "64x48, planar RGBA strips");
  int cnt[6] = {0};
  for (uint16_t y = 0; y < ti.h; y++) {
    uint8_t st;
    const uint32_t o[4] = {tiffRowOffset(ti, 0, y, st), tiffRowOffset(ti, 1, y, st),
                           tiffRowOffset(ti, 2, y, st), tiffRowOffset(ti, 3, y, st)};
    for (uint16_t x = 0; x < ti.w; x++)
      cnt[radarLevel(tif[o[0] + x * st], tif[o[1] + x * st], tif[o[2] + x * st], tif[o[3] + x * st])]++;
  }
  // expected counts come from PIL + the same n0q table (tools/radar/composite_n0q.txt)
  CHECK(cnt[0] == 2765 && cnt[1] == 177 && cnt[2] == 46 && cnt[3] == 55 && cnt[4] == 27 && cnt[5] == 2,
        "every pixel of the real frame lands on the same level as the Python reference");
  {   // the size the device parses: 320x240 planar, 40 strips of 25 rows (v3-R2 NTH 6)
    const std::vector<uint8_t> big = slurpBin("iem_n0q_320x240.tif");
    TiffInfo tb;
    CHECK(!big.empty() && tiffParse(big.data(), 2048, tb) && tb.w == 320 && tb.h == 240 && tb.planar &&
              tb.nStrips == 40 && tb.rowsPerStrip == 25,
          "full-size frame: 320x240 planar, 40 strips of 25 rows, parsed from 2 KB");
    CHECK(tiffDataEnd(tb) == big.size(), "full-size frame: the strips end exactly at the file size (completeness check)");
    CHECK(tiffDataEnd(tb) > big.size() / 2 + 1, "a half-downloaded file would fail the completeness check");
    int c[6] = {0}, misses = 0;
    for (uint16_t y = 0; y < tb.h; y++) {
      uint8_t st;
      uint32_t o[4];
      for (uint8_t p = 0; p < 4; p++) o[p] = tiffRowOffset(tb, p, y, st);
      for (uint16_t x = 0; x < tb.w; x++) {
        bool miss;
        c[radarLevel(big[o[0] + x], big[o[1] + x], big[o[2] + x], big[o[3] + x], &miss)]++;
        misses += miss;
      }
    }
    CHECK(c[0] == 68762 && c[1] == 4600 && c[2] == 1567 && c[3] == 1047 && c[4] == 755 && c[5] == 69 && !misses,
          "full-size frame: every level equals the Python reference, 0 colour misses");
  }
  const uint8_t bad[4] = {'M', 'M', 0, 42};
  CHECK(!tiffParse(bad, sizeof(bad), ti), "big-endian / truncated TIFF rejected");

  // ---- exact n0q colours at the level thresholds ----
  CHECK(radarLevel(67, 214, 126, 255) == 1, "20 dBZ colour -> light");
  CHECK(radarLevel(11, 132, 14, 255) == 2, "30 dBZ colour -> moderate");
  CHECK(radarLevel(255, 226, 0, 255) == 3, "40 dBZ colour -> heavy");
  CHECK(radarLevel(248, 0, 0, 255) == 4, "50 dBZ colour -> very heavy");
  CHECK(radarLevel(255, 234, 255, 255) == 5, "60 dBZ colour -> extreme");
  CHECK(radarLevel(133, 113, 143, 255) == 0, "-32 dBZ (clutter-ish) colour -> none");
  CHECK(radarLevel(248, 0, 0, 0) == 0, "alpha 0 -> none");
  bool miss = false;
  CHECK(radarLevel(249, 1, 1, 255, &miss) == 0 && miss, "a shifted ramp colour is a MISS, never guessed");
  CHECK(radarLevel(248, 0, 0, 255, &miss) == 4 && !miss, "an exact colour is not a miss");
  // clutter mask is generated from real dry frames and is not empty
  int masked = 0;
  for (uint16_t y = 0; y < RADAR_H; y++)
    for (uint16_t x = 0; x < RADAR_W; x++) masked += radarIsClutter(x, y);
  CHECK(masked > 50 && masked < 1000, "static clutter mask loaded (a few hundred px)");

  // ---- clutter cleaning ----
  MemRows m(40, 20);
  for (int i = 0; i < 8; i++) m.at(2 + i % 4, 2 + i / 4) = 1;             // 8 px speck -> dropped
  for (int i = 0; i < 14; i++) m.at(20 + i % 7, 3 + i / 7) = 2;           // 14 px blob -> kept
  for (int i = 0; i < 6; i++) m.at(5 + i, 12 + i) = 1;                    // diagonal line, 6 px ...
  for (int i = 0; i < 6; i++) m.at(11 + i, 17 - i) = 1;                   // ... joined into a V of 12 -> kept
  for (int y = 10; y < 16; y++) { m.at(30, y) = 1; m.at(34, y) = 1; }      // U: two arms ...
  for (int x = 30; x <= 34; x++) m.at(x, 16) = 1;                         // ... merge late (union)
  const int before = m.count();
  CHECK(radarClean(m, 40, 20, 12, nullptr), "cleaning runs");
  CHECK(m.at(2, 2) == 0 && m.at(5, 3) == 0, "8 px speck dropped (clutter)");
  CHECK(m.at(20, 3) == 2 && m.at(26, 4) == 2, "14 px blob kept");
  CHECK(RADAR_LV(m.at(5, 12)) == 1 && RADAR_LV(m.at(16, 12)) == 1, "8-connected diagonal V (12 px) kept");
  CHECK(RADAR_LV(m.at(30, 10)) == 1 && RADAR_LV(m.at(34, 10)) == 1, "U whose arms join only at the bottom kept (label union)");
  CHECK(m.count() == before - 8, "only the speck was removed");
  MemRows six(20, 10);
  for (int i = 0; i < 6; i++) six.at(2 + i % 3, 2 + i / 3) = 1;          // 6 px shower
  for (int i = 0; i < 5; i++) six.at(12 + i, 6) = 1;                     // 5 px speck
  CHECK(radarClean(six, 20, 10, 6, nullptr) && RADAR_LV(six.at(2, 2)) == 1 && six.at(12, 6) == 0,
        "at the 6 px floor: a 6 px shower survives, a 5 px speck is dropped");

  // ---- per-blob clutter mask + naming (v3-R2-2) ----
  struct Mask {  // a fake clutter site: x 30..32, y 5..8 (and "near" = the same box for the test)
    static bool near(uint16_t x, uint16_t y) { return x >= 28 && x <= 34 && y >= 3 && y <= 10; }
  };
  MemRows k(60, 20);
  for (int i = 0; i < 8; i++) k.at(29 + i % 4, 5 + i / 4) = 2;           // 8 px hugging the site
  for (int y = 2; y < 16; y++)
    for (int x = 22; x < 27; x++) k.at(x, y) = 1;                         // 70 px storm ...
  for (int x = 22; x < 40; x++) k.at(x, 12) = 3;                          // ... crossing the site (row 12 is outside 'near')
  for (int y = 9; y < 12; y++) k.at(33, y) = 3;                           // ... with a spur INTO the near box
  CHECK(radarClean(k, 60, 20, 6, Mask::near), "per-blob cleaning runs");
  CHECK(k.at(22, 2) == 1 && RADAR_LV(k.at(33, 10)) == 3, "a big storm crossing a mask site keeps EVERY pixel");
  MemRows hug(60, 20);
  for (int i = 0; i < 8; i++) hug.at(29 + i % 4, 5 + i / 4) = 2;
  radarClean(hug, 60, 20, 6, Mask::near);
  CHECK(hug.count() == 0, "a blob mostly near the clutter mask is dropped (not just its masked pixels)");
  // naming: a 7 px moderate speck is drawn but not named; the heavy storm is
  MemRows nm(RADAR_W, RADAR_H);
  for (int i = 0; i < 7; i++) nm.at(200 + i % 4, 116 + i / 4) = 2;       // speck, 17 mi E
  for (int y = 40; y < 60; y++)
    for (int x = 220; x < 250; x++) nm.at(x, y) = 3;                       // 600 px heavy storm NE
  radarClean(nm, RADAR_W, RADAR_H, 6, nullptr);
  CHECK(RADAR_LV(nm.at(200, 116)) == 2 && (nm.at(200, 116) & RADAR_UNNAMED), "7 px speck drawn but UNNAMED");
  float nmi, naz;
  CHECK(radarNearest(nm, RADAR_W, 214, 1, nmi, naz) && naz > 20 && naz < 70, "the words name the storm, not the speck");
  MemRows lt(RADAR_W, RADAR_H);
  for (int i = 0; i < 30; i++) lt.at(100 + i % 6, 100 + i / 6) = 1;      // 30 px light: virga-sized
  radarClean(lt, RADAR_W, RADAR_H, 6, nullptr);
  CHECK(!radarNearest(lt, RADAR_W, 214, 1, nmi, naz), "30 px of light rain is not named (virga rule)");
  // v3-R3-1: rain near you is always named, whatever its size
  MemRows here(RADAR_W, RADAR_H);
  for (int i = 0; i < 10; i++) here.at(RADAR_CX + 1 + i % 5, RADAR_CY - 1 + i / 5) = 2;   // 10 px moderate at ~1 mi
  radarClean(here, RADAR_W, RADAR_H, 6, nullptr);
  CHECK(radarNearest(here, RADAR_W, 214, 1, nmi, naz) && nmi < 2, "a small cell over you is named -> 'Raining here'");
  MemRows far(RADAR_W, RADAR_H);
  for (int i = 0; i < 9; i++) far.at(250 + i % 3, 116 + i / 3) = 2;                        // lone 9 px, 45 mi E
  radarClean(far, RADAR_W, RADAR_H, 6, nullptr);
  CHECK(!radarNearest(far, RADAR_W, 214, 1, nmi, naz) && far.count() == 9,
        "a lone far 9 px echo is drawn but unnamed (strip: 'Small echoes only', never 'No rain')");

  // ---- loop set on a fixed grid (reviewer H2) ----
  uint32_t ls[6];
  const uint32_t T = 1700000000u - 1700000000u % 600 + 300;   // newest at a :05 minute
  CHECK(radarLoopSet(T, ls, 6, 600) == 6 && ls[5] == T && ls[4] == T - 300 && ls[0] == T - 300 - 4 * 600,
        "loop set: the newest frame + 5 frames on the 10-min grid");
  uint32_t ls2[6];
  radarLoopSet(T + 300, ls2, 6, 600);                          // 5 min later (on the grid)
  int kept = 0;
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++) kept += ls[i] == ls2[j];
  CHECK(kept == 5, "loop set: a new frame 5 min later keeps 5 of 6 (no re-download of the loop)");

  // ---- nearest rain ----
  MemRows r(RADAR_W, RADAR_H);
  for (int y = 100; y < 110; y++)
    for (int x = 60; x < 70; x++) r.at(x, y) = 1;                          // rain W of you
  for (int y = 116; y < 120; y++)
    for (int x = 280; x < 290; x++) r.at(x, y) = 3;                        // heavy E
  float mi, az;
  CHECK(radarNearest(r, RADAR_W, 214, 1, mi, az) && az > 260 && az < 290, "nearest rain is to the W");
  CHECK(fabsf(mi - 91.0f / RADAR_PPN * 1.150779f) < 1.0f, "distance in statute miles");
  CHECK(radarNearest(r, RADAR_W, 214, 3, mi, az) && az > 80 && az < 100, "nearest heavy rain is to the E");
  CHECK(!radarNearest(r, RADAR_W, 90, 1, mi, az), "rain below maxY (under the strip) is ignored");
}
