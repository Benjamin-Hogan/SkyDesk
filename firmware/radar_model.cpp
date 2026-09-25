#include "radar_model.h"
#include "config.h"

#include <math.h>
#include <new>
#include <string.h>

// ---------------------------------------------------------------- colours
int16_t radarDbz2(uint8_t r, uint8_t g, uint8_t b) {
  const uint32_t key = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
  int lo = 0, hi = RADAR_RAMP_N - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    if (RADAR_RAMP[mid].rgb == key) return RADAR_RAMP[mid].dbz2;
    if (RADAR_RAMP[mid].rgb < key) lo = mid + 1;
    else hi = mid - 1;
  }
  return RADAR_MISS;
}

uint8_t radarLevel(uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool *miss) {
  if (miss) *miss = false;
  if (!a) return 0;
  static const int16_t TH2[5] = {RADAR_DBZ_LIGHT * 2, RADAR_DBZ_MODERATE * 2, RADAR_DBZ_HEAVY * 2,
                                 RADAR_DBZ_VHEAVY * 2, RADAR_DBZ_EXTREME * 2};
  const int16_t d = radarDbz2(r, g, b);
  if (d == RADAR_MISS) {
    if (miss) *miss = true;
    return 0;
  }
  uint8_t lv = 0;
  for (uint8_t k = 0; k < 5; k++)
    if (d >= TH2[k]) lv = k + 1;
  return lv;
}

// ---------------------------------------------------------------- TIFF
namespace {
uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t u32(const uint8_t *p) { return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
}  // namespace

bool tiffParse(const uint8_t *buf, size_t n, TiffInfo &ti) {
  memset(&ti, 0, sizeof(ti));
  if (n < 8 || buf[0] != 'I' || buf[1] != 'I' || u16(buf + 2) != 42) return false;   // little-endian only
  const uint32_t ifd = u32(buf + 4);
  if (ifd + 2 > n) return false;
  const uint16_t cnt = u16(buf + ifd);
  if (ifd + 2 + cnt * 12u > n) return false;
  uint16_t spp = 0, compression = 1, planarCfg = 1;
  uint32_t offCount = 0, offType = 0, offVal = 0, cntCount = 0, cntType = 0, cntVal = 0;
  bool bits8 = false;
  for (uint16_t i = 0; i < cnt; i++) {
    const uint8_t *e = buf + ifd + 2 + i * 12;
    const uint16_t tag = u16(e), type = u16(e + 2);
    const uint32_t count = u32(e + 4);
    const uint32_t val = type == 3 && count == 1 ? u16(e + 8) : u32(e + 8);
    switch (tag) {
      case 256: ti.w = (uint16_t)val; break;
      case 257: ti.h = (uint16_t)val; break;
      case 258:
        if (count == 1) bits8 = val == 8;
        else if (val + count * 2 <= n) {
          bits8 = true;
          for (uint32_t k = 0; k < count; k++) bits8 = bits8 && u16(buf + val + k * 2) == 8;
        }
        break;
      case 259: compression = (uint16_t)val; break;
      case 273: offCount = count; offType = type; offVal = u32(e + 8); break;
      case 279: cntCount = count; cntType = type; cntVal = u32(e + 8); break;
      case 277: spp = (uint16_t)val; break;
      case 278: ti.rowsPerStrip = (uint16_t)val; break;
      case 284: planarCfg = (uint16_t)val; break;
      default: break;
    }
  }
  if (!ti.w || !ti.h || !bits8 || compression != 1 || spp != 4 || !offCount) return false;
  if (!ti.rowsPerStrip || ti.rowsPerStrip > ti.h) ti.rowsPerStrip = ti.h;
  ti.planar = planarCfg == 2;
  const uint32_t perPlane = (ti.h + ti.rowsPerStrip - 1) / ti.rowsPerStrip;
  if (offCount != (ti.planar ? perPlane * 4 : perPlane) || offCount > 64) return false;
  ti.nStrips = (uint16_t)offCount;
  auto array = [&](uint32_t count, uint32_t type, uint32_t val, uint32_t *out) {
    const size_t esz = type == 3 ? 2 : 4;
    if (count == 1) { out[0] = type == 3 ? (val & 0xFFFF) : val; return true; }
    if (val + count * esz > n) return false;
    for (uint32_t k = 0; k < count; k++) out[k] = esz == 2 ? u16(buf + val + k * 2) : u32(buf + val + k * 4);
    return true;
  };
  if (!array(offCount, offType, offVal, ti.stripOff)) return false;
  if (cntCount == offCount && !array(cntCount, cntType, cntVal, ti.stripCnt)) return false;
  return true;
}

uint32_t tiffDataEnd(const TiffInfo &ti) {
  uint32_t end = 0;
  for (uint16_t k = 0; k < ti.nStrips; k++) {
    const uint32_t cnt = ti.stripCnt[k] ? ti.stripCnt[k] : (uint32_t)ti.rowsPerStrip * ti.w * (ti.planar ? 1 : 4);
    if (ti.stripOff[k] + cnt > end) end = ti.stripOff[k] + cnt;
  }
  return end;
}

uint8_t radarLoopSet(uint32_t newest, uint32_t *out, uint8_t frames, uint32_t step) {
  if (!newest || !frames) return 0;
  uint32_t tmp[16];
  uint8_t n = 0;
  tmp[n++] = newest;                                   // the newest, whatever its minute
  uint32_t g = newest - newest % step;                 // then the fixed grid below it
  if (g == newest) g -= step;
  while (n < frames && n < 16 && g) {
    tmp[n++] = g;
    g -= step;
  }
  for (uint8_t i = 0; i < n; i++) out[i] = tmp[n - 1 - i];   // oldest first
  return n;
}

uint32_t tiffRowOffset(const TiffInfo &ti, uint8_t plane, uint16_t y, uint8_t &step) {
  const uint16_t perPlane = (ti.h + ti.rowsPerStrip - 1) / ti.rowsPerStrip;
  const uint16_t inStrip = y % ti.rowsPerStrip;
  if (ti.planar) {
    step = 1;
    return ti.stripOff[plane * perPlane + y / ti.rowsPerStrip] + (uint32_t)inStrip * ti.w;
  }
  step = 4;
  return ti.stripOff[y / ti.rowsPerStrip] + (uint32_t)inStrip * ti.w * 4 + plane;
}

// ---------------------------------------------------------------- cleaning
namespace {
constexpr uint16_t MAX_LABELS = 1500;    // worst real frame: 827 runs (cap: scratch size / 8)
constexpr uint16_t D2_SAT = 8191;        // minD2 is 13 bits; 'near you' needs only ~100 px^2
constexpr uint16_t MAX_RUNS = RADAR_W / 2 + 1;
// minD2: px^2 to you, saturated at 65535 (only compared with the ~100 px^2 'near you' radius)
struct Run { uint16_t x0, x1, label; uint8_t maxLv; uint16_t near; uint16_t minD2; };   // 12 B
struct Blob { uint16_t parent, size, near; uint16_t minD2 : 13; uint16_t maxLv : 3; };   // 8 B

uint16_t findRoot(Blob *b, uint16_t a) {
  while (b[a].parent != a) { b[a].parent = b[b[a].parent].parent; a = b[a].parent; }
  return a;
}

uint16_t sat(uint32_t v) { return v > 0xFFFF ? 0xFFFF : (uint16_t)v; }

uint16_t runsOf(const uint8_t *row, uint16_t w, uint16_t y, RadarMaskFn nearMask, Run *out) {
  uint16_t n = 0;
  for (uint16_t x = 0; x < w && n < MAX_RUNS; x++) {
    if (!RADAR_LV(row[x])) continue;
    Run r{x, x, 0, 0, 0, D2_SAT};
    for (;; x++) {
      r.maxLv = r.maxLv > RADAR_LV(row[x]) ? r.maxLv : RADAR_LV(row[x]);
      if (nearMask && nearMask(x, y)) r.near++;
      if (x + 1 >= w || !RADAR_LV(row[x + 1])) break;
    }
    r.x1 = x;
    const int32_t dy = (int32_t)y - RADAR_CY;                      // closest pixel of the run to you
    const int32_t dx = RADAR_CX < r.x0 ? r.x0 - RADAR_CX : RADAR_CX > r.x1 ? RADAR_CX - r.x1 : 0;
    const uint32_t d2 = (uint32_t)(dx * dx + dy * dy);
    r.minD2 = d2 > D2_SAT ? D2_SAT : (uint16_t)d2;
    out[n++] = r;
  }
  return n;
}

// Label a row's runs from the previous row (8-connected: x ranges overlap after
// widening by 1). Pass 1 unions + accumulates blob stats; pass 2 only labels. Both
// allocate new labels in the same order, so the labels are identical.
bool labelRow(Run *cur, uint16_t nc, const Run *prev, uint16_t np, Blob *b, uint16_t cap, uint16_t &nLabels,
              bool pass1) {
  for (uint16_t i = 0; i < nc; i++) {
    int32_t lab = -1;
    for (uint16_t j = 0; j < np; j++) {
      if (prev[j].x1 + 1 < cur[i].x0 || prev[j].x0 > cur[i].x1 + 1) continue;
      if (lab < 0) {
        lab = prev[j].label;
        if (!pass1) break;
      } else if (pass1) {
        const uint16_t ra = findRoot(b, (uint16_t)lab), rb = findRoot(b, prev[j].label);
        if (ra != rb) {
          b[rb].parent = ra;
          b[ra].size = sat((uint32_t)b[ra].size + b[rb].size);
          b[ra].near = sat((uint32_t)b[ra].near + b[rb].near);
          if (b[rb].maxLv > b[ra].maxLv) b[ra].maxLv = b[rb].maxLv;
          if (b[rb].minD2 < b[ra].minD2) b[ra].minD2 = b[rb].minD2;
        }
      }
    }
    if (lab < 0) {
      if (nLabels >= cap) return false;
      lab = nLabels++;
      if (pass1) b[lab] = {(uint16_t)lab, 0, 0, D2_SAT, 0};
    }
    cur[i].label = (uint16_t)lab;
    if (pass1) {
      const uint16_t r = findRoot(b, (uint16_t)lab);
      b[r].size = sat((uint32_t)b[r].size + (cur[i].x1 - cur[i].x0 + 1));
      b[r].near = sat((uint32_t)b[r].near + cur[i].near);
      if (cur[i].maxLv > b[r].maxLv) b[r].maxLv = cur[i].maxLv;
      if (cur[i].minD2 < b[r].minD2) b[r].minD2 = cur[i].minD2;
    }
  }
  return true;
}
}  // namespace

int radarClean(RadarRows &rows, uint16_t w, uint16_t h, uint16_t minBlobPx, RadarMaskFn nearMask, void *blobMem,
               size_t blobLen, void *runMem, size_t runLen) {
  if (w > RADAR_W) return RADAR_CLEAN_OVERFLOW;
  // Caller scratch when given (the device lends idle buffers - no big allocation, RAM is
  // what TLS runs short of); otherwise the heap (host tests).
  const bool ownB = !blobMem || blobLen < 64 * sizeof(Blob);
  const bool ownR = !runMem || runLen < 2 * MAX_RUNS * sizeof(Run);
  Blob *b = ownB ? new (std::nothrow) Blob[MAX_LABELS] : static_cast<Blob *>(blobMem);
  const uint16_t cap = ownB ? MAX_LABELS : (uint16_t)min<size_t>(MAX_LABELS, blobLen / sizeof(Blob));
  Run *ra = ownR ? new (std::nothrow) Run[2 * MAX_RUNS] : static_cast<Run *>(runMem);
  Run *rb = ra ? ra + MAX_RUNS : nullptr;
  auto release = [&]() {
    if (ownB) delete[] b;
    if (ownR) delete[] ra;
  };
  if (!(b && ra)) {
    release();
    return RADAR_CLEAN_NOMEM;
  }
  bool ok = true;
  uint8_t row[RADAR_W];
  for (int pass = 1; ok && pass <= 2; pass++) {
    uint16_t nLabels = 0, np = 0;
    Run *prev = ra, *cur = rb;
    for (uint16_t y = 0; ok && y < h; y++) {
      ok = rows.read(y, row);
      if (!ok) break;
      const uint16_t nc = runsOf(row, w, y, nearMask, cur);
      ok = labelRow(cur, nc, prev, np, b, cap, nLabels, pass == 1);
      if (!ok) break;
      if (pass == 2) {
        bool changed = false;
        for (uint16_t i = 0; i < nc; i++) {
          const Blob &k = b[findRoot(b, cur[i].label)];
          const bool drop = k.size < minBlobPx || (uint32_t)k.near * 2 >= k.size;
          // rain near you is ALWAYS named, whatever its size (v3-R3-1): the size rule only
          // stops distant specks steering the headline
          const float nearPx = RADAR_NAME_ALWAYS_MI / 1.150779f * RADAR_PPN;
          const bool named = (k.size >= RADAR_NAME_MIN_PX && k.maxLv >= 2) || k.size >= RADAR_NAME_LIGHT_PX ||
                             k.minD2 <= (uint32_t)(nearPx * nearPx);
          for (uint16_t x = cur[i].x0; x <= cur[i].x1; x++) {
            const uint8_t v = drop ? 0 : (uint8_t)(RADAR_LV(row[x]) | (named ? 0 : RADAR_UNNAMED));
            changed |= v != row[x];
            row[x] = v;
          }
        }
        if (changed) ok = rows.write(y, row);
      }
      Run *t = prev; prev = cur; cur = t;
      np = nc;
    }
  }
  release();
  return ok ? RADAR_CLEAN_OK : RADAR_CLEAN_OVERFLOW;
}

bool radarNearest(RadarRows &rows, uint16_t w, uint16_t maxY, uint8_t minLevel, float &miles, float &azDeg) {
  uint8_t row[RADAR_W];
  float best = -1;
  int16_t bx = 0, by = 0;
  for (uint16_t y = 0; y < maxY; y++) {
    if (!rows.read(y, row)) return false;
    for (uint16_t x = 0; x < w; x++) {
      if ((row[x] & RADAR_UNNAMED) || RADAR_LV(row[x]) < minLevel || !RADAR_LV(row[x])) continue;
      const float d = hypotf((float)x - RADAR_CX, (float)y - RADAR_CY);
      if (best < 0 || d < best) { best = d; bx = x; by = y; }
    }
  }
  if (best < 0) return false;
  miles = best / RADAR_PPN * 1.150779f;
  azDeg = fmodf(atan2f((float)(bx - RADAR_CX), (float)(RADAR_CY - by)) * 57.2957795f + 360.0f, 360.0f);
  return true;
}
