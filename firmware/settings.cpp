#include "settings.h"
#include "config.h"
#include "setup_model.h"

#include <Preferences.h>

namespace {
Settings g_s;
}

Settings &settings() { return g_s; }

void settingsLoad() {
  Preferences p;
  p.begin("skydesk", true);
  g_s.viewUpDeg = p.getShort("viewUp", VIEW_UP_DEG_DEFAULT);
  g_s.nightDim = p.getBool("nightDim", true);
  g_s.touchCal = p.getBool("tcal", false);
  g_s.flip = p.getBool("flip", SCREEN_FLIP_DEFAULT);
  g_s.tXMin = p.getShort("txmin", TOUCH_RAW_MIN_X);
  g_s.tXMax = p.getShort("txmax", TOUCH_RAW_MAX_X);
  g_s.tYMin = p.getShort("tymin", TOUCH_RAW_MIN_Y);
  g_s.tYMax = p.getShort("tymax", TOUCH_RAW_MAX_Y);
  g_s.mapZoom = p.getUChar("mapZoom", MAP_DEFAULT_ZOOM);
  if (g_s.mapZoom > 2) g_s.mapZoom = MAP_DEFAULT_ZOOM;
  // Portal settings (docs/12): one blob, secrets.h as the defaults.
  PortalCfg &n = g_s.net;
  if (p.getBytes("pcfg", &n, sizeof(n)) != sizeof(n) || !setupCfgSane(n)) {   // NaN, bad strings -> defaults
    memset(&n, 0, sizeof(n));
    n.magic = PORTAL_MAGIC;
    strncpy(n.ssid, WIFI_SSID, sizeof(n.ssid) - 1);
    strncpy(n.pass, WIFI_PASSWORD, sizeof(n.pass) - 1);
    n.lat = OBS_LAT;
    n.lon = OBS_LON;
    n.elevFt = OBS_ELEV_FT;
    strncpy(n.place, OBS_PLACE, sizeof(n.place) - 1);
  }
  memset(g_s.apPw, 0, sizeof(g_s.apPw));
  p.getString("apPw", g_s.apPw, sizeof(g_s.apPw));
  p.end();
  bool havePw = strlen(g_s.apPw) == 8;
  for (const char *c = g_s.apPw; *c; ++c) havePw &= strchr(SETUP_PW_ALPHABET, *c) != nullptr;
  if (!havePw) {                                 // once per device: iOS remembers the network
    setupApPassword(esp_random, g_s.apPw);
    Preferences w;
    w.begin("skydesk", false);
    w.putString("apPw", g_s.apPw);
    w.end();
  }
  Serial.printf("[cfg] viewUp=%d nightDim=%d touchCal=%d\n", g_s.viewUpDeg, g_s.nightDim, g_s.touchCal);
}

void settingsSave() {
  Preferences p;
  p.begin("skydesk", false);
  p.putShort("viewUp", g_s.viewUpDeg);
  p.putBool("nightDim", g_s.nightDim);
  p.putBool("tcal", g_s.touchCal);
  p.putBool("flip", g_s.flip);
  p.putShort("txmin", g_s.tXMin);
  p.putShort("txmax", g_s.tXMax);
  p.putShort("tymin", g_s.tYMin);
  p.putShort("tymax", g_s.tYMax);
  p.putUChar("mapZoom", g_s.mapZoom);
  p.end();
}

void settingsSavePortal() {
  Preferences p;
  p.begin("skydesk", false);
  g_s.net.magic = PORTAL_MAGIC;
  p.putBytes("pcfg", &g_s.net, sizeof(g_s.net));
  p.end();
}

uint8_t screenRotation() { return g_s.flip ? (TFT_ROTATION + 2) & 3 : TFT_ROTATION; }

const char *settingsTzPosix() {
  const SetupTz *z = setupTzById(g_s.net.tz);
  return z ? z->posix : NTP_TZ;
}

void settingsRequestSetup(bool automatic) {
  Preferences p;
  p.begin("skydesk", false);
  p.putUChar("setupReq", automatic ? 2 : 1);
  p.end();
}

bool settingsTakeSetupRequest(bool &automatic) {
  Preferences p;
  p.begin("skydesk", false);
  const uint8_t r = p.getUChar("setupReq", 0);
  if (r) p.remove("setupReq");                   // cleared FIRST: a crash comes back normal
  p.end();
  automatic = r == 2;
  return r != 0;
}
