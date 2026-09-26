// setup_model.h - the setup portal's pure rules (docs/12-setup-portal.md): location
// parsing, the map gate, form validation, time zones, the auto-entry rule, the hotspot
// password and the WiFi-join QR string. Host-tested; no WiFi / NVS / drawing.
#pragma once

#include <stddef.h>
#include <stdint.h>

// "33.35280, -111.78900" or 33°21'10"N 111°47'20"W (UTF-8 or ASCII quotes, spaces,
// commas, N/S/E/W). False for junk or out of range.
bool setupParseLocation(const char *text, double &lat, double &lon);

double setupMilesBetween(double lat1, double lon1, double lat2, double lon2);
// The saved location is close enough to the build location for the built-in street maps
// (SETUP_GATE_MI) / for the radar basemap and clutter mask (SETUP_RADAR_GATE_MI, wider: a
// radar pixel is ~0.5 mi).
bool setupInGate(double lat, double lon, double buildLat, double buildLon);
bool setupInRadarGate(double lat, double lon, double buildLat, double buildLon);

struct SetupTz { const char *id, *label, *posix; };
extern const SetupTz SETUP_TZS[];
extern const uint8_t SETUP_TZ_N;
const SetupTz *setupTzById(const char *id);           // nullptr if not in the list

// What the setup portal saves (docs/12), as ONE NVS blob: a power cut can't leave half of it.
// Defaults come from secrets.h / config.h (WIFI_SSID, OBS_*, NTP_TZ).
struct PortalCfg {
  uint32_t magic;          // PORTAL_MAGIC: blob layout version
  char ssid[33];
  char pass[64];           // never sent back to the phone page
  bool locSaved;           // a location from the portal (else the build location)
  double lat, lon;
  bool elevKnown;          // elevFt came from Open-Meteo for this location
  float elevFt;
  char place[21];          // ASCII-folded; "" = none (show the coordinates)
  char tz[4];              // SETUP_TZS id; "" = the build's NTP_TZ
};
#define PORTAL_MAGIC 0x50434631u   // "PCF1"

struct SetupForm {
  const char *ssid, *pass;      // pass "" = keep the saved one (only if the SSID is unchanged)
  double lat, lon;
  bool locOk;
  const char *place;
  const char *tz;
  int facing;
};
// nullptr when valid, else a message for the page. NaN / inf locations are refused.
const char *setupValidate(const SetupForm &f, const char *savedSsid, bool hasSavedPass);

// Apply a VALID form to the saved config (review: the save rules, host-tested):
// - a blank password keeps the old one (validation allowed it only for the same SSID)
// - at the build location (1e-5 deg) -> locSaved = false; otherwise a custom location
// - a moved location re-learns its elevation (elevKnown = false, build elevation until then)
// - the Name is ASCII-folded; an empty Name at the build location becomes buildPlace
void setupApplySave(PortalCfg &c, const SetupForm &f, double buildLat, double buildLon, float buildElevFt,
                    const char *buildPlace);
// A saved config that can't be trusted (NaN, out of range, bad strings) -> false.
bool setupCfgSane(const PortalCfg &c);
// The SETUP_TZS id whose POSIX string is `posix`, else "az" (the page needs a pick).
const char *setupTzIdForPosix(const char *posix);
// Strict decimal parse for form fields ("33.5" ok; "", "33abc", "nan" -> false).
bool setupParseNumber(const char *s, double &out);

// WiFi disconnect reasons (esp_wifi_types.h). An ALLOWLIST of "the network is there and it
// refused us" (round 2 M1): AUTH_EXPIRE 2, 4WAY_HANDSHAKE_TIMEOUT 15, AUTH_FAIL 202,
// HANDSHAKE_TIMEOUT 204. Everything else - NO_AP_FOUND 201, BEACON_TIMEOUT 200 (router lost
// power), CONNECTION_FAIL 205, ASSOC_LEAVE 8 (our own disconnect) - is NOT "refusing".
#define SETUP_REASON_NO_AP_FOUND 201
bool setupReasonVisible(uint8_t reason);
// The refusal streak for auto-entry, fed from the WiFi EVENT (not sampled):
// - it counts only while refusals KEEP COMING: a gap > SETUP_REFUSAL_GAP_MS restarts it
//   (review R1: one AUTH_EXPIRE then 3 min of anything else must not open the portal)
// - it needs at least one password-type refusal (15, 202, 204): AUTH_EXPIRE 2 alone is also
//   weak signal / a busy AP
// - the network going away (200, 201) resets it; every other reason is neutral (ASSOC_LEAVE 8
//   from our own 12 s retry: round 3 M1)
bool setupReasonNeutral(uint8_t reason);   // neither extends nor resets, and isn't shown
#define SETUP_REFUSAL_GAP_MS 30000UL
struct SetupRefusal { uint32_t sinceMs, lastMs; bool strong; };
void setupNoteReason(SetupRefusal &r, uint8_t reason, uint32_t nowMs);
uint32_t setupRefusingMs(const SetupRefusal &r, uint32_t nowMs);
// The boot screen's words for a failed join: "Wrong password?", "Not found - 2.4 GHz only?", "".
const char *setupReasonWords(uint8_t reason);

// Auto-entry (docs/12 -> Modes): no SSID configured -> now; the network is visible (by the
// disconnect reason) but joining keeps failing for SETUP_AUTO_S -> yes; not visible -> never.
bool setupAutoEnter(bool ssidConfigured, uint8_t lastReason, uint32_t failingMs);

// 8 lowercase letters without i/l/o (look like 1/0) and r/v (rn/m, vv/w in fsb9): one keyboard
// page on an iPhone (no Shift, no 123). Generated once per device and kept in NVS, so iOS's
// remembered network keeps working.
#define SETUP_PW_ALPHABET "abcdefghjkmnpqstuwxyz"
void setupApPassword(uint32_t (*rnd)(), char out[9]);

// WIFI:T:WPA;S:<ssid>;P:<pass>;; with \ ; , : " escaped (the WIFI: QR format).
size_t setupWifiQr(const char *ssid, const char *pass, char *out, size_t n);
// A JSON string body (no quotes) with " \ and control characters escaped, and < > & as
// \u003c \u003e \u0026: the JSON is inlined into a <script> (review: a neighbour's SSID
// "</script>..." must not end it).
size_t setupJsonEscape(const char *in, char *out, size_t n);

// UTF-8 -> printable ASCII for the device fonts (round 2 M2): curly quotes -> ' ",
// dashes -> -, Latin-1 letters -> their base letter (e -> e, n~ -> n), anything else -> '?'.
size_t setupAsciiFold(const char *in, char *out, size_t n);
bool setupIsPlainAscii(const char *s);   // every byte is printable ASCII (drawable as-is)
