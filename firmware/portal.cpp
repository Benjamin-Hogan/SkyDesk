// Setup portal boot (docs/12-setup-portal.md). Layout: docs/mockups/portal_screen.py -> portal().
// The phone page is docs/mockups/portal/index.html, embedded byte-for-byte by
// tools/portal/embed_page.py (firmware/portal_page.cpp) with the state inlined at /*STATE*/.
#include "portal.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config.h"
#include "observer.h"
#include "settings.h"
#include "setup_model.h"
#include "touch_input.h"
#include "ui_internal.h"
extern "C" {
#include "qrcode.h"
}

extern const char PORTAL_PAGE_HEAD[] PROGMEM;
extern const char PORTAL_PAGE_TAIL[] PROGMEM;

using namespace ui;

namespace {

enum class St : uint8_t { Waiting, Joined, Page, Saved };

constexpr int16_t QR_X = 8, QR_Y = 34, QR_SCALE = 4, QR_QUIET = 4;
constexpr int16_t QR_BOX = (33 + 2 * QR_QUIET) * QR_SCALE;     // 164 px
constexpr int16_t COL_X = QR_X + QR_BOX + 12;                  // 184
constexpr int16_t CX = 8, CY = 202, CW = 112, CH = 36;         // Hold to cancel (>= 36 px)

struct Net { char ssid[33]; int8_t rssi; };

// Everything big is HEAP, allocated only by portalRun(): the normal boot pays 0 bytes of
// static RAM for the portal (docs/12 -> RAM; TLS needs every byte there).
WebServer *g_webp = nullptr;
DNSServer *g_dnsp = nullptr;
#define g_web (*g_webp)
#define g_dns (*g_dnsp)
constexpr size_t STATE_LEN = 2600, SCAN_LEN = 1400, MAX_NETS = 16;
char *g_stateBuf = nullptr, *g_scanBuf = nullptr;
bool g_auto = false;
char g_apSsid[24];
Net *g_nets = nullptr;
uint8_t g_nNets = 0;
St g_st = St::Waiting, g_drawnSt = St::Saved;
bool g_pageOpen = false;
uint32_t g_idleStart = 0, g_lastReq = 0, g_savedAt = 0, g_bootMs = 0;
uint32_t g_holdStart = 0, g_holdSeen = 0, g_nudgeUntil = 0;
int g_drawnMins = -1;
float g_drawnHold = -1;
bool g_drawnNudge = false;
char g_savedMsgSsid[40];

uint32_t idleLimitMs() { return (g_auto ? SETUP_AUTO_IDLE_S : SETUP_IDLE_S) * 1000UL; }

// ---- WiFi scan -------------------------------------------------------------------------
void scan() {
  const int n = WiFi.scanNetworks(false, false, false, 80);   // short dwell: the AP is off-channel meanwhile
  g_nNets = 0;
  for (int i = 0; i < n; i++) {
    const String s = WiFi.SSID(i);
    if (!s.length() || s.length() > 32) continue;                  // hidden: "Other..."
    const int8_t r = (int8_t)WiFi.RSSI(i);
    int at = -1;
    for (uint8_t k = 0; k < g_nNets; k++)
      if (strcmp(g_nets[k].ssid, s.c_str()) == 0) at = k;         // mesh: keep the strongest
    if (at >= 0) {
      if (r > g_nets[at].rssi) g_nets[at].rssi = r;
      continue;
    }
    if (g_nNets == MAX_NETS) continue;
    snprintf(g_nets[g_nNets].ssid, sizeof(g_nets[0].ssid), "%s", s.c_str());
    g_nets[g_nNets++].rssi = r;
  }
  WiFi.scanDelete();
  for (uint8_t a = 0; a < g_nNets; a++)                             // strongest first
    for (uint8_t b = a + 1; b < g_nNets; b++)
      if (g_nets[b].rssi > g_nets[a].rssi) {
        const Net t = g_nets[a];
        g_nets[a] = g_nets[b];
        g_nets[b] = t;
      }
  Serial.printf("[portal] scan: %u networks\n", g_nNets);
}

// ---- JSON (the inlined state) ------------------------------------------------------------
size_t appendf(char *buf, size_t n, size_t len, const char *fmt, ...) {
  if (len >= n) return len;
  va_list ap;
  va_start(ap, fmt);
  const int w = vsnprintf(buf + len, n - len, fmt, ap);
  va_end(ap);
  return w > 0 ? min(n - 1, len + (size_t)w) : len;
}

size_t netsJson(char *buf, size_t n, size_t len) {
  char esc[72];
  len = appendf(buf, n, len, "[");
  for (uint8_t i = 0; i < g_nNets; i++) {
    setupJsonEscape(g_nets[i].ssid, esc, sizeof(esc));
    len = appendf(buf, n, len, "%s[\"%s\",%d]", i ? "," : "", esc, g_nets[i].rssi);
  }
  return appendf(buf, n, len, "]");
}

size_t stateJson(char *buf, size_t n) {
  const PortalCfg &c = settings().net;
  char ssid[72], place[48], bplace[48];
  setupJsonEscape(c.ssid, ssid, sizeof(ssid));
  setupJsonEscape(c.place[0] ? c.place : (c.locSaved ? "" : OBS_PLACE), place, sizeof(place));
  setupJsonEscape(OBS_PLACE, bplace, sizeof(bplace));
  size_t len = appendf(buf, n, 0, "var S={\"ssid\":\"%s\",\"hasPass\":%s,\"nets\":", ssid, c.pass[0] ? "true" : "false");
  len = netsJson(buf, n, len);
  const SetupTz *tz = setupTzById(c.tz);
  len = appendf(buf, n, len,
                ",\"lat\":%.5f,\"lon\":%.5f,\"place\":\"%s\",\"buildLat\":%.5f,\"buildLon\":%.5f,\"buildPlace\":\"%s\","
                "\"gateMi\":%.2f,\"radarGateMi\":%.2f,\"tz\":\"%s\",\"tzs\":[",
                obs().lat, obs().lon, place, obsBuildLat(), obsBuildLon(), bplace, (double)SETUP_GATE_MI,
                (double)SETUP_RADAR_GATE_MI, tz ? tz->id : setupTzIdForPosix(NTP_TZ));
  for (uint8_t i = 0; i < SETUP_TZ_N; i++)
    len = appendf(buf, n, len, "%s[\"%s\",\"%s\"]", i ? "," : "", SETUP_TZS[i].id, SETUP_TZS[i].label);
  return appendf(buf, n, len, "],\"facing\":%d};", (int)((settings().viewUpDeg % 360 + 360) % 360));
}

// ---- HTTP -----------------------------------------------------------------------------------
void noCache() {
  g_web.sendHeader("Cache-Control", "no-store");
  g_web.sendHeader("Connection", "close");
}

void sendPage() {
  char *state = g_stateBuf;                  // heap, setup boot only (no TLS here)
  stateJson(state, STATE_LEN);
  noCache();
  g_web.setContentLength(CONTENT_LENGTH_UNKNOWN);
  g_web.send(200, "text/html; charset=utf-8", "");
  g_web.sendContent_P(PORTAL_PAGE_HEAD);
  g_web.sendContent(state);
  g_web.sendContent_P(PORTAL_PAGE_TAIL);
  g_web.sendContent("");
}

void handlePage() {                          // "/": a person asked for it
  g_lastReq = millis();
  sendPage();
}

void handleProbe() {                         // captive probes / unknown paths: the sheet opens,
  sendPage();                                // but an auto-joined iPhone's probes don't keep the
}                                            // session alive (review R2)

void handleScan() {
  g_lastReq = millis();
  scan();
  char *buf = g_scanBuf;
  size_t len = appendf(buf, SCAN_LEN, 0, "{\"nets\":");
  len = netsJson(buf, SCAN_LEN, len);
  appendf(buf, SCAN_LEN, len, "}");
  noCache();
  g_web.send(200, "application/json", buf);
}

void handleOpened() {                        // the page rendered on the phone (round 2)
  g_lastReq = millis();
  g_pageOpen = true;
  g_web.send(204);
}

void reply(bool ok, const char *msg) {
  char esc[240], body[280];
  setupJsonEscape(msg, esc, sizeof(esc));
  snprintf(body, sizeof(body), "{\"ok\":%s,\"msg\":\"%s\"}", ok ? "true" : "false", esc);
  noCache();
  g_web.send(200, "application/json", body);
}

void handleSave() {                          // NOTE: the body is never logged (it holds the password)
  g_lastReq = millis();
  PortalCfg &c = settings().net;
  // Each arg held in its own String (arg() returns by value: no pointers into temporaries).
  const String ssid = g_web.arg("ssid"), pass = g_web.arg("pass"), placeIn = g_web.arg("place"), tz = g_web.arg("tz");
  const String latS = g_web.arg("lat"), lonS = g_web.arg("lon"), facS = g_web.arg("facing");
  double lat = 0, lon = 0, fac = -1;
  const bool locOk = setupParseNumber(latS.c_str(), lat) && setupParseNumber(lonS.c_str(), lon);
  if (!setupParseNumber(facS.c_str(), fac) || fac != (int)fac) fac = -1;   // "abc", "45.9" refused
  SetupForm f{ssid.c_str(), pass.c_str(), lat, lon, locOk, placeIn.c_str(), tz.c_str(), (int)fac};
  if (const char *err = setupValidate(f, c.ssid, c.pass[0] != '\0')) {
    reply(false, err);
    return;
  }
  // One NVS blob for the portal fields (a power cut can't leave half of it), then facing.
  setupApplySave(c, f, obsBuildLat(), obsBuildLon(), OBS_ELEV_FT, OBS_PLACE);   // host-tested rules
  settingsSavePortal();
  settings().viewUpDeg = (int16_t)f.facing;
  settingsSave();
  Serial.printf("[portal] saved: network \"%s\", location %s, tz %s\n", c.ssid, c.locSaved ? "custom" : "build", c.tz);

  char msg[220];
  snprintf(msg, sizeof(msg),
           "SkyDesk is restarting and joining %s. You can close this page. If it can't join, tap Set up from "
           "phone on SkyDesk.",
           c.ssid);
  reply(true, msg);
  snprintf(g_savedMsgSsid, sizeof(g_savedMsgSsid), "%s", setupIsPlainAscii(c.ssid) ? c.ssid : "");
  g_st = St::Saved;
  g_savedAt = millis();
}

// ---- Drawing (portal_screen.py) -----------------------------------------------------------
void drawQr() {
  TFT_eSPI &g = *tft;
  char text[120];
  setupWifiQr(g_apSsid, settings().apPw, text, sizeof(text));
  QRCode qr;
  uint8_t buf[qrcode_getBufferSize(4)];
  g.fillRect(QR_X, QR_Y, QR_BOX, QR_BOX, TFT_WHITE);   // dark on light, with a quiet zone
  if (qrcode_initText(&qr, buf, 4, ECC_MEDIUM, text) != 0) return;
  const int16_t o = QR_QUIET * QR_SCALE;
  for (uint8_t y = 0; y < qr.size; y++)
    for (uint8_t x = 0; x < qr.size; x++)
      if (qrcode_getModule(&qr, x, y))
        g.fillRect(QR_X + o + x * QR_SCALE, QR_Y + o + y * QR_SCALE, QR_SCALE, QR_SCALE, TFT_BLACK);
}

void drawCancel(float hold, bool nudge) {
  TFT_eSPI &g = *tft;
  g.fillRoundRect(CX, CY, CW, CH, CH / 2, COL_PANEL2);
  if (hold > 0) g.fillRoundRect(CX, CY, max<int16_t>(CH, (int16_t)(CW * min(hold, 1.0f))), CH, CH / 2, COL_PLANE_DIM);
  drawText(g, nudge ? "Keep holding" : "Hold to cancel", CX + CW / 2, CY + 23, Font::F2, nudge ? COL_PLANE : COL_TEXT,
           C_BASELINE);
}

void drawStatus(int minsLeft) {             // the right column's top + the bottom-right lines
  TFT_eSPI &g = *tft;
  const int16_t x = COL_X;
  g.fillRect(x, 34, SCREEN_W - x, 109, COL_BG);   // stops above NETWORK's glcd top row (143)
  g.fillRect(CX + CW + 4, 204, SCREEN_W - (CX + CW + 4), 36, COL_BG);
  if (g_st == St::Joined || g_st == St::Page) {
    drawCheck(g, x, 50, COL_OK);
    if (g_st == St::Joined) {
      drawText(g, "Phone joined", x, 82, Font::F2, COL_TEXT);
      drawText(g, "No page? Open", x, 100, Font::F2, COL_MUTED);
      drawText(g, "Safari to", x, 116, Font::F2, COL_MUTED);
      drawText(g, "192.168.4.1", x, 132, Font::F2, COL_TEXT);
    } else {
      drawText(g, "Page open", x, 82, Font::F2, COL_TEXT);
      drawText(g, "Finish on your", x, 100, Font::F2, COL_MUTED);
      drawText(g, "phone", x, 116, Font::F2, COL_MUTED);
    }
    return;                                  // joined: the 30-min session rule, no countdown
  }
  drawText(g, "ON YOUR PHONE", x, 44, Font::Glcd, COL_DIM);
  static const char *STEPS[] = {"Open Camera", "Aim at the code", "Tap Join", "Page opens"};
  for (int i = 0; i < 4; i++) {
    const int16_t y = 62 + i * 19;
    g.fillCircle(x + 6, y - 5, 7, COL_MUTED);
    char n[2] = {(char)('1' + i), 0};
    drawText(g, n, x + 6, y, Font::Glcd, COL_BG, C_BASELINE);
    drawText(g, STEPS[i], x + 18, y, Font::F2, COL_TEXT);
  }
  drawText(g, "or open 192.168.4.1", 312, 220, Font::F2, COL_MUTED, R_BASELINE);
  char buf[24];
  snprintf(buf, sizeof(buf), "closes in %d min", minsLeft);
  drawText(g, buf, 312, 234, Font::Glcd, COL_DIM, R_BASELINE);
}

void drawAll() {
  TFT_eSPI &g = *tft;
  g.fillScreen(COL_BG);
  const PortalCfg &c = settings().net;
  if (g_auto) {
    char t[56];
    snprintf(t, sizeof(t), "Can't join \"%s\"", c.ssid);
    setFont(g, Font::Fsb9);
    if (!setupIsPlainAscii(c.ssid) || g.textWidth(t) > 300) snprintf(t, sizeof(t), "Can't join your WiFi");
    drawText(g, t, 10, 22, Font::Fsb9, COL_WARN);
  } else {
    drawText(g, "Phone setup", 10, 24, Font::Fsb12, COL_TEXT);
  }
  drawQr();
  drawText(g, "NETWORK", COL_X, 150, Font::Glcd, COL_DIM);
  drawText(g, g_apSsid, COL_X, 168, Font::F2, COL_TEXT);
  drawText(g, "PASSWORD", COL_X, 182, Font::Glcd, COL_DIM);
  drawNumber(g, settings().apPw, COL_X, 200, Font::Fsb9, COL_TEXT);
  drawCancel(0, false);
  g_drawnSt = St::Saved;                     // force drawStatus
  g_drawnMins = -1;
}

void drawSaved() {
  TFT_eSPI &g = *tft;
  g.fillScreen(COL_BG);
  drawCheck(g, 150, 84, COL_OK);
  drawText(g, "Saved", 160, 128, Font::Fsb12, COL_TEXT, C_BASELINE);
  char msg[64];
  snprintf(msg, sizeof(msg), "Restarting, joining \"%s\"", g_savedMsgSsid);
  setFont(g, Font::F2);
  if (!g_savedMsgSsid[0] || g.textWidth(msg) > 300) snprintf(msg, sizeof(msg), "Restarting, joining your WiFi");
  drawText(g, msg, 160, 152, Font::F2, COL_MUTED, C_BASELINE);
}

}  // namespace

void portalRun(TFT_eSPI &t, bool automatic) {
  tft = &t;
  g_auto = automatic;
  g_webp = new WebServer(80);
  g_dnsp = new DNSServer();
  g_stateBuf = (char *)malloc(STATE_LEN);
  g_scanBuf = (char *)malloc(SCAN_LEN);
  g_nets = (Net *)calloc(MAX_NETS, sizeof(Net));
  if (!g_webp || !g_dnsp || !g_stateBuf || !g_scanBuf || !g_nets) {   // can't happen (~40 KB free here)
    Serial.println("[portal] out of memory");
    delay(1000);
    ESP.restart();
  }
  analogWrite(TFT_BL, BL_FULL);                // a dimmed QR scans worse (round 1 S10)
  uint8_t mac[6];
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect();
  scan();                                      // before the AP is up: nothing to disturb
  WiFi.softAPmacAddress(mac);
  snprintf(g_apSsid, sizeof(g_apSsid), "SkyDesk-Setup-%02X%02X", mac[4], mac[5]);
  const IPAddress ip(192, 168, 4, 1);
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  WiFi.softAP(g_apSsid, settings().apPw);
  g_dns.setErrorReplyCode(DNSReplyCode::NoError);
  g_dns.start(53, "*", ip);                    // every name -> us: the captive-portal sheet opens
  g_web.on("/", HTTP_GET, handlePage);
  g_web.on("/scan", HTTP_GET, handleScan);
  g_web.on("/opened", HTTP_POST, handleOpened);
  g_web.on("/save", HTTP_POST, handleSave);
  g_web.onNotFound(handleProbe);               // /hotspot-detect.html, /generate_204, ...
  g_web.begin();
  Serial.printf("[portal] %s up (%s entry), 8-bit heap %u\n", g_apSsid, automatic ? "automatic" : "manual",
                heap_caps_get_free_size(MALLOC_CAP_8BIT));
  drawAll();
  g_idleStart = g_lastReq = g_bootMs = millis();

  for (;;) {
    g_dns.processNextRequest();
    g_web.handleClient();
    const uint32_t now = millis();

    if (g_st == St::Saved) {
      if (g_drawnSt != St::Saved || g_savedAt == now) drawSaved();
      g_drawnSt = St::Saved;
      if (now - g_savedAt > 3000) ESP.restart();
      delay(10);
      continue;
    }
    const uint8_t stations = WiFi.softAPgetStationNum();
    g_st = g_pageOpen ? St::Page : stations ? St::Joined : St::Waiting;
    if (!stations) g_pageOpen = false;         // the phone left: back to the steps

    // Timeouts (docs/12 -> Modes): idle with no phone; an abandoned joined session.
    if (stations) g_idleStart = now;
    if (now - g_idleStart > idleLimitMs()) ESP.restart();
    if (stations && now - g_lastReq > SETUP_SESSION_S * 1000UL) ESP.restart();
    // An automatic entry never holds the device offline for long unless someone is actually
    // using the page (review R2: iOS may auto-rejoin the remembered hotspot).
    if (g_auto && !g_pageOpen && now - g_bootMs > SETUP_AUTO_CAP_S * 1000UL) ESP.restart();

    // Hold to cancel: the fill starts on touch-down; dropouts < 150 ms don't reset it.
    int16_t x, y;
    const bool down = touchPoint(x, y);
    if (down) g_idleStart = now;               // a touch extends the idle timer
    const bool onBtn = down && x >= CX && x < CX + CW && y >= CY - 6 && y < CY + CH;
    if (onBtn) {
      if (!g_holdStart) g_holdStart = now;
      g_holdSeen = now;
    } else if (g_holdStart && now - g_holdSeen > 150) {
      g_holdStart = 0;
      g_nudgeUntil = now + 1500;               // released early
    }
    const float hold = g_holdStart ? (now - g_holdStart) / (float)SETUP_HOLD_MS : 0;
    if (hold >= 1.0f) {
      Serial.println("[portal] cancelled");
      ESP.restart();
    }
    const bool nudge = !g_holdStart && (int32_t)(g_nudgeUntil - now) > 0;
    if (fabsf(hold - g_drawnHold) > 0.04f || nudge != g_drawnNudge) {
      drawCancel(hold, nudge);
      g_drawnHold = hold;
      g_drawnNudge = nudge;
    }
    const int mins = (int)((idleLimitMs() - min(idleLimitMs(), now - g_idleStart) + 59999) / 60000);
    if (g_st != g_drawnSt || (g_st == St::Waiting && mins != g_drawnMins)) {
      drawStatus(mins);
      g_drawnSt = g_st;
      g_drawnMins = mins;
    }
    delay(5);
  }
}
