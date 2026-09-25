#include "ui.h"
#include "config.h"

#include <math.h>

const uint16_t PALETTE16[16] = {
  COL_BG, COL_PANEL, COL_PANEL2, COL_HAIR, COL_TEXT, COL_MUTED, COL_DIM, COL_PLANE,
  COL_PLANE_DIM, COL_PLANE_FAINT, COL_OK, COL_WARN, COL_ERR, COL_RAIN, COL_CLOUD, COL_SUN,
};

void setFont(TFT_eSPI &g, Font f) {
  switch (f) {
    case Font::Glcd:  g.setTextFont(1); break;
    case Font::F2:    g.setTextFont(2); break;
    case Font::Fs9:   g.setFreeFont(&FreeSans9pt7b); break;
    case Font::Fsb9:  g.setFreeFont(&FreeSansBold9pt7b); break;
    case Font::Fsb12: g.setFreeFont(&FreeSansBold12pt7b); break;
    case Font::Fsb18: g.setFreeFont(&FreeSansBold18pt7b); break;
    case Font::Fsb24: g.setFreeFont(&FreeSansBold24pt7b); break;
  }
  g.setTextSize(1);
}

int16_t capHeight(Font f) {
  switch (f) {
    case Font::Glcd:  return 7;
    case Font::F2:    return 9;
    case Font::Fs9:
    case Font::Fsb9:  return 12;
    case Font::Fsb12: return 17;
    case Font::Fsb18: return 25;
    case Font::Fsb24: return 33;
  }
  return 12;
}

int16_t drawText(TFT_eSPI &g, const char *s, int16_t x, int16_t y, Font f, uint16_t c, uint8_t datum) {
  // TFT_eSPI quirk: for the GLCD font (font 1, no free font) the *_BASELINE
  // datums leave `baseline` at 0, so y is treated as the TOP of the text. Every
  // layout here (and screens.py) uses y = baseline, and GLCD's baseline is 7 px
  // below its top, so shift it ourselves. Without this, every micro label lands
  // 7 px low and overlaps the value under it.
  if (f == Font::Glcd && (datum == L_BASELINE || datum == C_BASELINE || datum == R_BASELINE))
    y -= 7;
  setFont(g, f);
  g.setTextColor(c);
  g.setTextDatum(datum);
  g.setTextPadding(0);
  const int16_t w = g.textWidth(s);
  g.drawString(s, x, y);
  return w;
}

static int16_t trackFor(Font f) { return f == Font::Fsb9 ? 1 : 0; }

int16_t numberWidth(TFT_eSPI &g, const char *s, Font f) {
  setFont(g, f);
  const size_t n = strlen(s);
  return g.textWidth(s) + trackFor(f) * (n > 0 ? (int16_t)n - 1 : 0);
}

int16_t drawNumber(TFT_eSPI &g, const char *s, int16_t x, int16_t y, Font f, uint16_t c, uint8_t datum) {
  const int16_t tr = trackFor(f);
  if (!tr) return drawText(g, s, x, y, f, c, datum);
  const int16_t w = numberWidth(g, s, f);
  if (datum == C_BASELINE) x -= w / 2;
  else if (datum == R_BASELINE) x -= w;
  g.setTextColor(c);
  g.setTextDatum(L_BASELINE);
  char one[2] = {0, 0};
  for (const char *p = s; *p; ++p) {
    one[0] = *p;
    g.drawString(one, x, y);
    x += g.textWidth(one) + tr;
  }
  return w;
}

namespace {
struct Ring { int16_t r, thick, adv; };
Ring ringFor(Font f) {
  switch (f) {
    case Font::Fsb24: return {5, 3, 13};
    case Font::Fsb18: return {4, 2, 11};
    case Font::Fsb12: return {3, 2, 9};
    default:          return {2, 1, 7};
  }
}
}  // namespace

void drawDegreeRing(TFT_eSPI &g, int16_t x, int16_t yBase, Font f, uint16_t c) {
  const Ring r = ringFor(f);
  const int16_t top = yBase - capHeight(f);
  for (int16_t t = 0; t < r.thick; t++) g.drawCircle(x + 1 + r.r, top + r.r, r.r - t, c);
}

int16_t drawDegrees(TFT_eSPI &g, int value, int16_t x, int16_t y, Font f, uint16_t c, uint8_t datum) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", value);
  const int16_t w = numberWidth(g, buf, f);
  const int16_t total = w + ringFor(f).adv;
  if (datum == C_BASELINE) x -= total / 2;
  else if (datum == R_BASELINE) x -= total;
  drawNumber(g, buf, x, y, f, c);
  drawDegreeRing(g, x + w, y, f, c);
  return total;
}

void drawArrowRight(TFT_eSPI &g, int16_t x, int16_t yMid, int16_t len, uint16_t c, int16_t head, bool thin) {
  const int16_t sx = x + len - head - 1;
  if (thin) g.drawFastHLine(x, yMid, len - head, c);
  else g.fillRect(x, yMid - 1, len - head, 2, c);
  g.fillTriangle(sx, yMid - head, x + len, yMid, sx, yMid + head, c);
}

void drawSep(TFT_eSPI &g, int16_t x, int16_t y, uint16_t c, int16_t r) { g.fillCircle(x, y, r, c); }

void drawCheck(TFT_eSPI &g, int16_t x, int16_t y, uint16_t c) {
  for (int d = 0; d < 2; d++) {
    g.drawLine(x, y + 4 + d, x + 3, y + 7 + d, c);
    g.drawLine(x + 3, y + 7 + d, x + 9, y + d, c);
  }
}

void drawCross(TFT_eSPI &g, int16_t x, int16_t y, uint16_t c) {
  for (int d = 0; d < 2; d++) {
    g.drawLine(x + d, y, x + 8 + d, y + 8, c);
    g.drawLine(x + 8 + d, y, x + d, y + 8, c);
  }
}

void drawChevron(TFT_eSPI &g, int16_t x, int16_t yMid, uint16_t c) {
  for (int d = 0; d < 2; d++) {
    g.drawLine(x + d, yMid - 4, x + 4 + d, yMid, c);
    g.drawLine(x + 4 + d, yMid, x + d, yMid + 4, c);
  }
}

int16_t drawFit(TFT_eSPI &g, const char *s, int16_t x, int16_t y, int16_t maxW, uint16_t c,
                const Font *fonts, uint8_t nFonts) {
  for (uint8_t i = 0; i < nFonts; i++) {
    setFont(g, fonts[i]);
    if (g.textWidth(s) <= maxW) return drawText(g, s, x, y, fonts[i], c);
  }
  const Font f = fonts[nFonts - 1];
  setFont(g, f);
  char buf[48];
  strncpy(buf, s, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  size_t n = strlen(buf);
  while (n > 0 && g.textWidth(buf) + 9 > maxW) buf[--n] = '\0';
  while (n > 0 && buf[n - 1] == ' ') buf[--n] = '\0';
  const int16_t w = drawText(g, buf, x, y, f, c);
  for (int i = 0; i < 3; i++) g.fillRect(x + w + 1 + i * 3, y - 2, 2, 2, c);
  return w + 9;
}

// ---------------------------------------------------------------------------
//  Aircraft glyph - same parts as PLANE_PARTS in screens.py
// ---------------------------------------------------------------------------
namespace {
struct Pt { float x, y; };
const Pt NOSE[] = {{-1.6f, -9.5f}, {0, -12}, {1.6f, -9.5f}};
const Pt FUSE[] = {{-1.6f, -9.5f}, {1.6f, -9.5f}, {1.2f, 9}, {-1.2f, 9}};
const Pt RWING[] = {{1.4f, -3}, {10.5f, 3}, {10.5f, 5.5f}, {1.4f, 2}};
const Pt LWING[] = {{-1.4f, -3}, {-10.5f, 3}, {-10.5f, 5.5f}, {-1.4f, 2}};
const Pt RTAIL[] = {{1, 6.5f}, {4.5f, 9.5f}, {4.5f, 11}, {0.8f, 10}};
const Pt LTAIL[] = {{-1, 6.5f}, {-4.5f, 9.5f}, {-4.5f, 11}, {-0.8f, 10}};

void fillConvex(TFT_eSPI &g, const Pt *p, int n, float cx, float cy, float ca, float sa, float k, uint16_t c) {
  auto X = [&](const Pt &q) { return (int32_t)lroundf(cx + (q.x * ca - q.y * sa) * k); };
  auto Y = [&](const Pt &q) { return (int32_t)lroundf(cy + (q.x * sa + q.y * ca) * k); };
  for (int i = 1; i + 1 < n; i++)   // triangle fan
    g.fillTriangle(X(p[0]), Y(p[0]), X(p[i]), Y(p[i]), X(p[i + 1]), Y(p[i + 1]), c);
}
}  // namespace

void drawPlaneGlyph(TFT_eSPI &g, float cx, float cy, float headingDeg, float scale, uint16_t c) {
  const float a = headingDeg * DEG_TO_RAD, ca = cosf(a), sa = sinf(a);
  fillConvex(g, NOSE, 3, cx, cy, ca, sa, scale, c);
  fillConvex(g, FUSE, 4, cx, cy, ca, sa, scale, c);
  fillConvex(g, RWING, 4, cx, cy, ca, sa, scale, c);
  fillConvex(g, LWING, 4, cx, cy, ca, sa, scale, c);
  fillConvex(g, RTAIL, 4, cx, cy, ca, sa, scale, c);
  fillConvex(g, LTAIL, 4, cx, cy, ca, sa, scale, c);
}

// ---------------------------------------------------------------------------
//  Weather icons - same shapes as screens.py (sun, moon, cloud, rain...)
// ---------------------------------------------------------------------------
namespace {
int16_t R(float v) { return (int16_t)lroundf(v); }

void sun(TFT_eSPI &g, float cx, float cy, float s, uint16_t bg) {
  g.fillCircle(R(cx), R(cy), R(9 * s), COL_SUN);
  for (int i = 0; i < 8; i++) {
    const float a = i * 45 * DEG_TO_RAD;
    g.drawWideLine(cx + cosf(a) * 13 * s, cy + sinf(a) * 13 * s, cx + cosf(a) * 17 * s,
                   cy + sinf(a) * 17 * s, fmaxf(1.5f, 2.5f * s), COL_SUN, bg);
  }
}

void moon(TFT_eSPI &g, float cx, float cy, float s, uint16_t bg) {
  g.fillCircle(R(cx), R(cy), R(9 * s), COL_MOON);
  g.fillCircle(R(cx + 5 * s), R(cy - 4 * s), R(8 * s), bg);   // bite = container colour
}

void cloud(TFT_eSPI &g, float cx, float cy, float s, uint16_t c = COL_CLOUD) {
  g.fillCircle(R(cx - 6 * s), R(cy + 2 * s), R(6 * s), c);
  g.fillCircle(R(cx + 2 * s), R(cy - 2 * s), R(8 * s), c);
  g.fillCircle(R(cx + 9 * s), R(cy + 3 * s), R(5 * s), c);
  g.fillRoundRect(R(cx - 12 * s), R(cy + 2 * s), R(26 * s), R(6 * s) + 1, R(3 * s), c);
}

void drops(TFT_eSPI &g, float cx, float cy, float s, uint16_t c, uint16_t bg) {
  for (int dx : {-5, 1, 7})
    g.drawWideLine(cx + dx * s, cy + 8 * s, cx + (dx - 2) * s, cy + 13 * s, fmaxf(1.5f, 2 * s), c, bg);
}
}  // namespace

void drawWxIcon(TFT_eSPI &g, int16_t cx, int16_t cy, float s, WxKind k, bool day, uint16_t bg) {
  switch (k) {
    case WxKind::Clear:
      if (day) sun(g, cx, cy, s, bg); else moon(g, cx, cy, s, bg);
      break;
    case WxKind::Partly:
      if (day) sun(g, cx - 4 * s, cy - 5 * s, s * 0.9f, bg);
      else moon(g, cx - 5 * s, cy - 5 * s, s * 0.9f, bg);
      cloud(g, cx + 5 * s, cy + 5 * s, s * 0.8f);
      break;
    case WxKind::Cloud:
      cloud(g, cx, cy, s);
      break;
    case WxKind::Fog:
      for (int i = 0; i < 3; i++)
        g.fillRoundRect(R(cx - 12 * s), R(cy - 6 * s + i * 6 * s), R(24 * s), R(3 * s) + 1, 1, COL_CLOUD);
      break;
    case WxKind::Drizzle:
    case WxKind::Rain:
      cloud(g, cx, cy - 3 * s, s);
      drops(g, cx, cy, s, COL_RAIN, bg);
      break;
    case WxKind::Snow:
      cloud(g, cx, cy - 3 * s, s);
      for (int dx : {-5, 1, 7}) g.fillCircle(R(cx + dx * s), R(cy + 10 * s), R(fmaxf(1, 1.6f * s)), COL_TEXT);
      break;
    case WxKind::Storm:
      cloud(g, cx, cy - 3 * s, s, COL_MUTED);
      g.fillTriangle(R(cx + 1 * s), R(cy + 4 * s), R(cx - 4 * s), R(cy + 11 * s), R(cx + 1 * s), R(cy + 10 * s), COL_SUN);
      g.fillTriangle(R(cx + 1 * s), R(cy + 9 * s), R(cx + 5 * s), R(cy + 9 * s), R(cx - 2 * s), R(cy + 17 * s), COL_SUN);
      break;
  }
}
