// Host tests for the setup portal's pure rules: setup_model.cpp (docs/12-setup-portal.md).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#include "config.h"
#include "setup_model.h"
extern "C" {
#include "qrcode.h"
}

extern int g_fail;
#define CHECK(cond, msg)                                                             \
  do {                                                                               \
    if (!(cond)) { std::printf("  FAIL: %s (line %d)\n", msg, __LINE__); g_fail++; } \
    else std::printf("  ok:   %s\n", msg);                                          \
  } while (0)

namespace {
uint32_t g_seed = 1;
uint32_t lcg() { return g_seed = g_seed * 1103515245u + 12345u; }
bool near(double a, double b) { return std::fabs(a - b) < 1e-5; }
}  // namespace

void testSetup() {
  std::printf("setup: location parser\n");
  double la, lo;
  CHECK(setupParseLocation("33.35280, -111.78900", la, lo) && near(la, 33.35280) && near(lo, -111.78900), "decimal");
  CHECK(setupParseLocation("  33.35280 -111.78900 ", la, lo) && near(lo, -111.78900), "decimal, spaces only");
  // the owner's exact string (UTF-8 degree sign, ASCII quote marks)
  CHECK(setupParseLocation("33\xC2\xB0" "21'10\"N 111\xC2\xB0" "47'20\"W", la, lo) && near(la, 33 + 21 / 60.0 + 10 / 3600.0) &&
            near(lo, -(111 + 47 / 60.0 + 20 / 3600.0)),
        "DMS with degree sign and quotes");
  CHECK(setupParseLocation("33 21 10 N, 111 47 20 W", la, lo) && la > 33.35 && lo < -111.78, "DMS with spaces");
  CHECK(setupParseLocation("111\xC2\xB0" "47'20\"W 33\xC2\xB0" "21'10\"N", la, lo) && la > 33 && lo < -111, "DMS lon first");
  // iOS smart punctuation turns ' and " into curly quotes (round 1 M3)
  CHECK(setupParseLocation("33\xC2\xB0" "21\xE2\x80\x99" "10\xE2\x80\x9D" "N 111\xC2\xB0" "47\xE2\x80\x99" "20\xE2\x80\x9D" "W", la, lo) &&
            near(la, 33 + 21 / 60.0 + 10 / 3600.0) && near(lo, -(111 + 47 / 60.0 + 20 / 3600.0)),
        "DMS typed on an iPhone (curly quotes)");
  CHECK(setupParseLocation("40.5N 73.99W", la, lo) && near(la, 40.5) && near(lo, -73.99), "decimal degrees + hemispheres");
  CHECK(!setupParseLocation("33.3, banana", la, lo), "junk rejected");
  CHECK(!setupParseLocation("95, 10", la, lo), "latitude out of range");
  CHECK(!setupParseLocation("33 75 00 N 111 W", la, lo), "minutes >= 60 rejected");
  CHECK(!setupParseLocation("33.3", la, lo), "one number rejected");
  CHECK(!setupParseLocation("33 N 44 N", la, lo), "two latitudes rejected");

  std::printf("setup: gate\n");
  CHECK(setupInGate(33.35280, -111.78900, 33.35280, -111.78900), "same point: in the gate");
  CHECK(setupInGate(33.3532, -111.7893, 33.35280, -111.78900), "a few metres: in");
  CHECK(!setupInGate(33.3818, -111.7890, 33.35280, -111.78900), "2 mi away: out");
  CHECK(!setupInGate(33.3586, -111.7890, 33.35280, -111.78900) && setupInRadarGate(33.3586, -111.7890, 33.35280, -111.78900),
        "0.4 mi: no streets, radar kept (round 1 M4)");
  CHECK(!setupInRadarGate(33.3818, -111.7890, 33.35280, -111.78900), "2 mi: radar off too");
  CHECK(std::fabs(setupMilesBetween(33.35280, -111.78900, 40.7486, -73.9864) - 2127) < 15, "Gilbert - NYC ~2,127 mi");

  std::printf("setup: form validation\n");
  SetupForm f{"HomeNet", "", 33.35280, -111.78900, true, "Gilbert, AZ", "az", 0};
  CHECK(setupValidate(f, "HomeNet", true) == nullptr, "blank password keeps the saved one (same SSID)");
  CHECK(setupValidate(f, "OtherNet", true) != nullptr, "blank password for a NEW network is refused");
  f.pass = "short";
  CHECK(setupValidate(f, "HomeNet", true) != nullptr, "7-char password refused");
  f.pass = "longenough";
  f.tz = "mars";
  CHECK(setupValidate(f, "", false) != nullptr, "unknown time zone refused");
  f.tz = "et";
  f.facing = 360;
  CHECK(setupValidate(f, "", false) != nullptr, "facing 360 refused");
  f.facing = 359;
  f.locOk = false;
  CHECK(setupValidate(f, "", false) != nullptr, "unreadable location refused");
  f.locOk = true;
  f.ssid = "";
  CHECK(setupValidate(f, "", false) != nullptr, "empty SSID refused");
  f.ssid = "123456789012345678901234567890123";
  CHECK(setupValidate(f, "", false) != nullptr, "33-byte SSID refused");
  CHECK(setupTzById("az") && !strcmp(setupTzById("az")->posix, "MST7"), "Arizona = MST7, no DST");

  std::printf("setup: auto-entry rule\n");
  CHECK(setupAutoEnter(false, 0, 0), "no SSID configured: at once");
  CHECK(!setupAutoEnter(true, SETUP_REASON_NO_AP_FOUND, 3600000), "NO_AP_FOUND (router off): never");
  CHECK(!setupAutoEnter(true, 202, SETUP_AUTO_S * 1000UL - 1), "AUTH_FAIL < 3 min: not yet");
  CHECK(setupAutoEnter(true, 202, SETUP_AUTO_S * 1000UL), "AUTH_FAIL 3 min: enter");
  CHECK(setupAutoEnter(true, 15, SETUP_AUTO_S * 1000UL), "4-way handshake timeout 3 min: enter");
  for (uint8_t r : {8, 200, 201, 205})
    CHECK(!setupAutoEnter(true, r, 3600000), "a non-refusing reason (8/200/201/205) never enters (round 2 M1)");
  SetupRefusal rf{};
  for (uint32_t t = 1000; t <= 101000; t += 10000) setupNoteReason(rf, 202, t);
  CHECK(setupRefusingMs(rf, 101000) == 100000, "refusals every 10 s: the streak counts from the first");
  setupNoteReason(rf, 201, 105000);
  CHECK(setupRefusingMs(rf, 106000) == 0, "the streak resets when the network goes away (201)");
  SetupRefusal one{};
  setupNoteReason(one, 202, 1000);
  CHECK(setupRefusingMs(one, 181000) == 0, "ONE refusal then 3 min of silence/neutral: no streak (review R1)");
  SetupRefusal weak{};
  for (uint32_t t = 1000; t <= 200000; t += 5000) setupNoteReason(weak, 2, t);
  CHECK(setupRefusingMs(weak, 200000) == 0, "AUTH_EXPIRE alone (weak signal, busy AP) never counts");
  // round 3 M1: our own 12 s retry reports ASSOC_LEAVE (8) between the router's refusals
  SetupRefusal alt{};
  bool entered = false;
  for (uint32_t t = 0; t <= 200000 && !entered; t += 6000) {
    const uint8_t reason = (t / 6000) % 2 ? 8 : 202;
    if (!setupReasonNeutral(reason)) setupNoteReason(alt, reason, t + 1);
    entered = setupAutoEnter(true, 202, setupRefusingMs(alt, t + 1));
  }
  CHECK(entered, "202/8 alternating over 200 s: the portal opens (8 is neutral)");
  CHECK(setupReasonNeutral(8) && setupReasonNeutral(205) && !setupReasonNeutral(200) && !setupReasonNeutral(202),
        "neutral reasons: 8, 205 ...; not 200/201/refusals");
  CHECK(!strcmp(setupReasonWords(202), "Wrong password?") && !strcmp(setupReasonWords(201), "Not found - 2.4 GHz only?") &&
            !setupReasonWords(0)[0],
        "boot words follow the real reason (round 1 S3)");

  std::printf("setup: hotspot password, QR string, JSON\n");
  bool clean = true;
  for (int k = 0; k < 200; k++) {
    char pw[9];
    setupApPassword(lcg, pw);
    clean &= strlen(pw) == 8 && !strpbrk(pw, "0O1IlL");
  }
  CHECK(clean, "8 chars, no 0/O/1/I/l/L");
  bool lower = true;
  for (int k = 0; k < 200; k++) {
    char pw[9];
    setupApPassword(lcg, pw);
    for (const char *c = pw; *c; ++c) lower &= *c >= 'a' && *c <= 'z' && !strchr("ilorv", *c);
  }
  CHECK(lower, "lowercase letters only: one iPhone keyboard page");
  CHECK(!strpbrk(SETUP_PW_ALPHABET, "\\;,:\""), "the alphabet never needs QR escaping");
  char q[160];
  setupWifiQr("My;Net,\"x\"", "p:a\\ss", q, sizeof(q));
  CHECK(!strcmp(q, "WIFI:T:WPA;S:My\\;Net\\,\\\"x\\\";P:p\\:a\\\\ss;;"), "WIFI: escapes \\ ; , : \"");
  char j[64];
  setupJsonEscape("a\"b\\c\n", j, sizeof(j));
  CHECK(!strcmp(j, "a\\\"b\\\\c\\u000a"), "JSON escape");

  std::printf("setup: review fixes - NaN, strict numbers, JSON in <script>, save rules\n");
  SetupForm nf{"HomeNet", "longenough", NAN, -111.7, true, "", "az", 0};
  CHECK(setupValidate(nf, "", false) != nullptr, "NaN latitude refused");
  double num;
  CHECK(setupParseNumber("33.5", num) && num == 33.5, "strict number: 33.5");
  CHECK(!setupParseNumber("", num) && !setupParseNumber("33abc", num) && !setupParseNumber("nan", num),
        "strict number: '', '33abc', 'nan' refused");
  char js[96];
  setupJsonEscape("</script><b>&", js, sizeof(js));
  CHECK(!strchr(js, '<') && !strchr(js, '>') && !strchr(js, '&'), "JSON: < > & escaped (inlined in <script>)");
  PortalCfg pc{};
  pc.magic = PORTAL_MAGIC;
  strcpy(pc.ssid, "HomeNet");
  strcpy(pc.pass, "oldpassword");
  pc.lat = 33.35280; pc.lon = -111.78900; pc.elevFt = 1253; strcpy(pc.place, "Gilbert, AZ");
  SetupForm sf{"HomeNet", "", 33.35280, -111.78900, true, "Home", "az", 90};
  setupApplySave(pc, sf, 33.35280, -111.78900, 1253, "Gilbert, AZ");
  CHECK(!strcmp(pc.pass, "oldpassword") && !pc.locSaved && !strcmp(pc.place, "Home"),
        "save at the build centre: password kept, not a custom location, the Name is kept (review)");
  sf.lat = 40.74861; sf.lon = -73.98639; sf.place = "Caf\xC3\xA9 roof";
  setupApplySave(pc, sf, 33.35280, -111.78900, 1253, "Gilbert, AZ");
  CHECK(pc.locSaved && !pc.elevKnown && !strcmp(pc.place, "Cafe roof"), "moved: custom, elevation re-learned, Name folded");
  pc.elevKnown = true; pc.elevFt = 30;
  setupApplySave(pc, sf, 33.35280, -111.78900, 1253, "Gilbert, AZ");
  CHECK(pc.elevKnown && pc.elevFt == 30, "the same custom spot again keeps its learned elevation");
  sf.lat = 33.35280; sf.lon = -111.78900; sf.place = "";
  setupApplySave(pc, sf, 33.35280, -111.78900, 1253, "Gilbert, AZ");
  CHECK(!pc.locSaved && !pc.elevKnown && !strcmp(pc.place, "Gilbert, AZ"), "back home, empty Name -> the home name");
  CHECK(setupCfgSane(pc), "a normal config is sane");
  pc.lat = NAN;
  CHECK(!setupCfgSane(pc), "a NaN config is not (boot falls back to the build location)");
  CHECK(!strcmp(setupTzIdForPosix("MST7"), "az") && !strcmp(setupTzIdForPosix("EST5EDT,M3.2.0,M11.1.0"), "et"),
        "time-zone default follows NTP_TZ");
  SetupForm lf{"HomeNet", "longenough", 33.3, -111.7, true, "Caf\xC3\xA9 at Ben\xE2\x80\x99s house", "az", 0};
  CHECK(setupValidate(lf, "", false) == nullptr, "a 19-char Name with an accent and a curly quote passes (folded)");

  std::printf("setup: ASCII fold (round 2 M2)\n");
  char fo[64];
  setupAsciiFold("Ben\xE2\x80\x99s desk", fo, sizeof(fo));
  CHECK(!strcmp(fo, "Ben's desk"), "curly apostrophe -> '");
  setupAsciiFold("Caf\xC3\xA9 Espa\xC3\xB1" "a \xE2\x80\x9Cguest\xE2\x80\x9D", fo, sizeof(fo));
  CHECK(!strcmp(fo, "Cafe Espana \"guest\""), "accents and curly double quotes");
  setupAsciiFold("Net \xF0\x9F\x93\xB6", fo, sizeof(fo));
  CHECK(!strcmp(fo, "Net ?") && setupIsPlainAscii(fo), "emoji -> ?, result drawable");
  CHECK(!setupIsPlainAscii("Caf\xC3\xA9") && setupIsPlainAscii("HomeNet"), "plain-ASCII check (SSID -> 'your WiFi')");

  std::printf("setup: QR modules == the mock's (qrcodegen, v4 ECC M)\n");
  QRCode qr;
  uint8_t buf[256];
  const char *text = "WIFI:T:WPA;S:SkyDesk-Setup-7F3A;P:K7PXM4QR;;";
  const bool made = qrcode_initText(&qr, buf, 4, ECC_MEDIUM, text) == 0;
  std::ifstream fx(FIXTURES "/qr_wifi_v4m.txt");
  std::string line, got, want;
  while (std::getline(fx, line)) want += line;
  for (uint8_t y = 0; y < qr.size; y++)
    for (uint8_t x = 0; x < qr.size; x++) got += qrcode_getModule(&qr, x, y) ? '1' : '0';
  CHECK(made && qr.size == 33, "version 4 = 33 modules");
  CHECK(got == want, "device QR matches the mockup, module for module");
  if (got != want) {
    int diff = 0;
    for (size_t i = 0; i < got.size() && i < want.size(); i++) diff += got[i] != want[i];
    std::printf("        %d modules differ\n", diff);
  }
}
