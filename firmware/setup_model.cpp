#include "setup_model.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

// POSIX TZ strings for the portal's short list (US rules since 2007).
const SetupTz SETUP_TZS[] = {
    {"az", "Arizona (no DST)", "MST7"},
    {"pt", "Pacific", "PST8PDT,M3.2.0,M11.1.0"},
    {"mt", "Mountain", "MST7MDT,M3.2.0,M11.1.0"},
    {"ct", "Central", "CST6CDT,M3.2.0,M11.1.0"},
    {"et", "Eastern", "EST5EDT,M3.2.0,M11.1.0"},
    {"ak", "Alaska", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"hi", "Hawaii", "HST10"},
};
const uint8_t SETUP_TZ_N = sizeof(SETUP_TZS) / sizeof(SETUP_TZS[0]);

const SetupTz *setupTzById(const char *id) {
  for (const auto &z : SETUP_TZS)
    if (id && strcmp(z.id, id) == 0) return &z;
  return nullptr;
}

namespace {

// Numbers (up to 3 per coordinate: degrees, minutes, seconds) and hemisphere letters; every
// other byte (degree signs, primes, quotes, commas, UTF-8 continuation bytes) separates.
struct Tok { bool isNum; double v; char hemi; bool neg; };

int tokenize(const char *s, Tok *out, int max) {
  int n = 0;
  while (*s && n < max) {
    const unsigned char c = (unsigned char)*s;
    if (isdigit(c) || ((c == '-' || c == '+' || c == '.') && (isdigit((unsigned char)s[1]) || s[1] == '.'))) {
      char *end;
      const double v = strtod(s, &end);
      if (end == s) return -1;
      out[n++] = {true, fabs(v), 0, v < 0 || *s == '-'};
      s = end;
    } else if (c == 'N' || c == 'S' || c == 'E' || c == 'W' || c == 'n' || c == 's' || c == 'e' || c == 'w') {
      out[n++] = {false, 0, (char)toupper(c), false};
      s++;
    } else if (isalpha(c)) {
      return -1;                                     // "banana"
    } else {
      s++;
    }
  }
  return *s ? -1 : n;
}

}  // namespace

bool setupParseLocation(const char *text, double &lat, double &lon) {
  if (!text) return false;
  Tok t[10];
  const int n = tokenize(text, t, 10);
  if (n <= 0) return false;
  bool anyHemi = false;
  for (int i = 0; i < n; i++) anyHemi |= !t[i].isNum;
  if (!anyHemi) {                                     // decimal pair
    if (n != 2) return false;
    lat = t[0].neg ? -t[0].v : t[0].v;
    lon = t[1].neg ? -t[1].v : t[1].v;
  } else {                                            // D [M [S]] H  D [M [S]] H
    double vals[2] = {0, 0};
    char hemi[2] = {0, 0};
    int k = 0, parts = 0;
    for (int i = 0; i < n; i++) {
      if (k > 1) return false;
      if (t[i].isNum) {
        if (parts >= 3 || t[i].neg) return false;
        const double scale = parts == 0 ? 1.0 : parts == 1 ? 1.0 / 60 : 1.0 / 3600;
        if (parts > 0 && t[i].v >= 60) return false;
        vals[k] += t[i].v * scale;
        parts++;
      } else {
        if (!parts) return false;
        hemi[k++] = t[i].hemi;
        parts = 0;
      }
    }
    if (k != 2 || parts) return false;
    const bool latFirst = hemi[0] == 'N' || hemi[0] == 'S';
    const char hLat = latFirst ? hemi[0] : hemi[1], hLon = latFirst ? hemi[1] : hemi[0];
    if (!(hLat == 'N' || hLat == 'S') || !(hLon == 'E' || hLon == 'W')) return false;
    lat = (latFirst ? vals[0] : vals[1]) * (hLat == 'S' ? -1 : 1);
    lon = (latFirst ? vals[1] : vals[0]) * (hLon == 'W' ? -1 : 1);
  }
  return lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180;
}

double setupMilesBetween(double lat1, double lon1, double lat2, double lon2) {
  const double r = 3.14159265358979323846 / 180, x = sin((lat2 - lat1) * r / 2), y = sin((lon2 - lon1) * r / 2);
  const double h = x * x + cos(lat1 * r) * cos(lat2 * r) * y * y;
  return 3958.8 * 2 * asin(sqrt(h));
}

bool setupInGate(double lat, double lon, double buildLat, double buildLon) {
  return setupMilesBetween(lat, lon, buildLat, buildLon) <= SETUP_GATE_MI;
}

bool setupInRadarGate(double lat, double lon, double buildLat, double buildLon) {
  return setupMilesBetween(lat, lon, buildLat, buildLon) <= SETUP_RADAR_GATE_MI;
}

bool setupReasonVisible(uint8_t reason) {
  return reason == 2 || reason == 15 || reason == 202 || reason == 204;   // an allowlist (round 2 M1)
}

bool setupReasonNeutral(uint8_t reason) { return !setupReasonVisible(reason) && reason != 200 && reason != 201; }

void setupNoteReason(SetupRefusal &r, uint8_t reason, uint32_t nowMs) {
  if (reason == 200 || reason == 201) {                               // the network is gone
    r = SetupRefusal{};
    return;
  }
  if (!setupReasonVisible(reason)) return;                            // neutral
  if (!r.sinceMs || nowMs - r.lastMs > SETUP_REFUSAL_GAP_MS) {        // a new streak
    r.sinceMs = nowMs ? nowMs : 1;
    r.strong = false;
  }
  r.lastMs = nowMs;
  if (reason == 15 || reason == 202 || reason == 204) r.strong = true;
}

uint32_t setupRefusingMs(const SetupRefusal &r, uint32_t nowMs) {
  if (!r.sinceMs || !r.strong || nowMs - r.lastMs > SETUP_REFUSAL_GAP_MS) return 0;
  return nowMs - r.sinceMs;
}

const char *setupReasonWords(uint8_t reason) {
  switch (reason) {
    case 2:     // AUTH_EXPIRE
    case 15:    // 4WAY_HANDSHAKE_TIMEOUT
    case 202:   // AUTH_FAIL
    case 204:   // HANDSHAKE_TIMEOUT
      return "Wrong password?";
    case SETUP_REASON_NO_AP_FOUND:
      return "Not found - 2.4 GHz only?";
    default:
      return "";
  }
}

const char *setupValidate(const SetupForm &f, const char *savedSsid, bool hasSavedPass) {
  const size_t sl = f.ssid ? strlen(f.ssid) : 0, pl = f.pass ? strlen(f.pass) : 0;
  if (sl == 0 || sl > 32) return "Pick a network, or type its name.";
  if (pl && (pl < 8 || pl > 63)) return "WiFi passwords are 8 to 63 characters.";
  if (!pl && !(hasSavedPass && savedSsid && strcmp(savedSsid, f.ssid) == 0)) return "Enter the WiFi password.";
  if (!f.locOk || !(f.lat >= -90 && f.lat <= 90) || !(f.lon >= -180 && f.lon <= 180))   // NaN fails too
    return "The location isn't readable.";
  if (f.place) {                                  // the device draws the FOLDED name (round 3)
    char folded[64];
    setupAsciiFold(f.place, folded, sizeof(folded));
    if (strlen(folded) > 20) return "The name is at most 20 characters.";
  }
  if (!setupTzById(f.tz)) return "Pick a time zone from the list.";
  if (f.facing < 0 || f.facing > 359) return "Facing is 0 to 359 degrees.";
  return nullptr;
}

bool setupAutoEnter(bool ssidConfigured, uint8_t lastReason, uint32_t failingMs) {
  if (!ssidConfigured) return true;
  return setupReasonVisible(lastReason) && failingMs >= SETUP_AUTO_S * 1000UL;
}

void setupApPassword(uint32_t (*rnd)(), char out[9]) {
  static const char A[] = SETUP_PW_ALPHABET;
  const uint32_t n = sizeof(A) - 1;
  for (int i = 0; i < 8; i++) out[i] = A[rnd() % n];
  out[8] = '\0';
}

namespace {
size_t putEsc(const char *s, char *out, size_t n, size_t len) {
  for (; s && *s && len + 2 < n; ++s) {
    if (strchr("\\;,:\"", *s)) out[len++] = '\\';
    out[len++] = *s;
  }
  out[len] = '\0';
  return len;
}
}  // namespace

size_t setupWifiQr(const char *ssid, const char *pass, char *out, size_t n) {
  size_t len = (size_t)snprintf(out, n, "WIFI:T:WPA;S:");
  len = putEsc(ssid, out, n, len);
  len += (size_t)snprintf(out + len, n - len, ";P:");
  len = putEsc(pass, out, n, len);
  len += (size_t)snprintf(out + len, n - len, ";;");
  return len;
}

size_t setupJsonEscape(const char *in, char *out, size_t n) {
  size_t len = 0;
  for (; in && *in && len + 7 < n; ++in) {
    const unsigned char c = (unsigned char)*in;
    if (c == '"' || c == '\\') {
      out[len++] = '\\';
      out[len++] = (char)c;
    } else if (c < 0x20 || c == '<' || c == '>' || c == '&') {
      len += (size_t)snprintf(out + len, n - len, "\\u%04x", c);
    } else {
      out[len++] = (char)c;
    }
  }
  out[len] = '\0';
  return len;
}

bool setupIsPlainAscii(const char *s) {
  for (; s && *s; ++s)
    if ((unsigned char)*s < 32 || (unsigned char)*s > 126) return false;
  return true;
}

size_t setupAsciiFold(const char *in, char *out, size_t n) {
  // Latin-1 supplement U+00C0..U+00FF -> base letters (x for the multiply/divide signs)
  static const char L1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
  size_t len = 0;
  const unsigned char *p = (const unsigned char *)in;
  while (p && *p && len + 1 < n) {
    uint32_t cp;
    int k;
    if (*p < 0x80) { cp = *p; k = 1; }
    else if ((*p & 0xE0) == 0xC0 && p[1]) { cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F); k = 2; }
    else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) { cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); k = 3; }
    else if ((*p & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) { cp = 0x10000; k = 4; }
    else { cp = '?'; k = 1; }
    char c;
    if (cp >= 32 && cp < 127) c = (char)cp;
    else if (cp == 0x2018 || cp == 0x2019 || cp == 0x2032 || cp == 0x00B4) c = '\'';
    else if (cp == 0x201C || cp == 0x201D || cp == 0x2033) c = '"';
    else if (cp == 0x2013 || cp == 0x2014 || cp == 0x2212) c = '-';
    else if (cp == 0x00A0) c = ' ';
    else if (cp >= 0xC0 && cp <= 0xFF) c = L1[cp - 0xC0];
    else c = '?';
    out[len++] = c;
    p += k;
  }
  out[len] = '\0';
  return len;
}

void setupApplySave(PortalCfg &c, const SetupForm &f, double buildLat, double buildLon, float buildElevFt,
                    const char *buildPlace) {
  snprintf(c.ssid, sizeof(c.ssid), "%s", f.ssid);
  if (f.pass && f.pass[0]) snprintf(c.pass, sizeof(c.pass), "%s", f.pass);
  const bool atBuild = fabs(f.lat - buildLat) < 1e-5 && fabs(f.lon - buildLon) < 1e-5;
  const bool moved = fabs(f.lat - c.lat) > 1e-5 || fabs(f.lon - c.lon) > 1e-5 || c.locSaved == atBuild;
  c.lat = f.lat;
  c.lon = f.lon;
  c.locSaved = !atBuild;
  if (moved) {
    c.elevKnown = false;
    c.elevFt = buildElevFt;
  }
  char folded[64];
  setupAsciiFold(f.place ? f.place : "", folded, sizeof(folded));
  size_t n = strlen(folded);
  while (n && folded[n - 1] == ' ') folded[--n] = '\0';
  snprintf(c.place, sizeof(c.place), "%s", folded[0] ? folded : (atBuild ? buildPlace : ""));
  snprintf(c.tz, sizeof(c.tz), "%s", f.tz ? f.tz : "");
  c.magic = PORTAL_MAGIC;
}

bool setupCfgSane(const PortalCfg &c) {
  if (c.magic != PORTAL_MAGIC) return false;
  if (!memchr(c.ssid, 0, sizeof(c.ssid)) || !memchr(c.pass, 0, sizeof(c.pass)) || !memchr(c.place, 0, sizeof(c.place)) ||
      !memchr(c.tz, 0, sizeof(c.tz)))
    return false;
  if (!(c.lat >= -90 && c.lat <= 90) || !(c.lon >= -180 && c.lon <= 180)) return false;
  if (!(c.elevFt > -1500 && c.elevFt < 30000)) return false;
  return !c.tz[0] || setupTzById(c.tz);
}

const char *setupTzIdForPosix(const char *posix) {
  for (const auto &z : SETUP_TZS)
    if (posix && strcmp(z.posix, posix) == 0) return z.id;
  return "az";
}

bool setupParseNumber(const char *s, double &out) {
  if (!s || !*s) return false;
  char *end;
  out = strtod(s, &end);
  return end != s && *end == '\0' && out == out && out < 1e9 && out > -1e9;   // out == out: not NaN
}
