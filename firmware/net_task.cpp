#include "net_task.h"
#include <esp_heap_caps.h>
#include "adsb_client.h"
#include "app_state.h"
#include "route_client.h"
#include "weather_client.h"
#include "http_json.h"
#include "map_model.h"
#include "radar_client.h"
#include "today_store.h"
#include "observer.h"
#include "settings.h"
#include "setup_model.h"

#include <WiFi.h>
#if ENABLE_OTA
#include <ArduinoOTA.h>
#endif

namespace {

NetStatus g_net;
Traffic  &g_traffic = appTrafficShared();   // the ONE shared copy: written under appTrafficLock()
bool      g_radarOwesPoll = false;   // a radar frame ran: an ADS-B poll must come next
uint32_t  g_radarDeferSince = 0;     // millis() when radar work was first deferred (0 = not)
uint32_t  g_radarDeferredMs = 0;     // deferred time this hour (logged)
uint32_t  g_radarDeferLogMs = 0;
Weather   g_weather;

void publishNet() { appSetNet(g_net); }

// docs/12: the last disconnect reason says whether the network is there at all
// (NO_AP_FOUND) or refusing us (AUTH_*, handshake timeouts) - the boot words and the
// automatic portal entry both follow it.
volatile uint8_t g_lastReason = 0;
SetupRefusal g_refusal{};          // how long the network has been REFUSING us (setup_model)
portMUX_TYPE g_refusalMux = portMUX_INITIALIZER_UNLOCKED;   // written by the WiFi event task

void onWifiEvent(arduino_event_id_t e, arduino_event_info_t info) {
  if (e != ARDUINO_EVENT_WIFI_STA_DISCONNECTED) return;
  const uint8_t r = info.wifi_sta_disconnected.reason;
  if (setupReasonNeutral(r)) return;               // our own disconnect (8) keeps the last real reason
  g_lastReason = r;
  const uint32_t now = millis();
  portENTER_CRITICAL(&g_refusalMux);
  setupNoteReason(g_refusal, r, now);              // every event counts, none is missed by sampling
  portEXIT_CRITICAL(&g_refusalMux);
}

uint32_t refusingMs() {
  portENTER_CRITICAL(&g_refusalMux);
  const uint32_t ms = setupRefusingMs(g_refusal, millis());
  portEXIT_CRITICAL(&g_refusalMux);
  return ms;
}

void wifiBegin() {
  static bool hooked = false;
  if (!hooked) {
    WiFi.onEvent(onWifiEvent);
    hooked = true;
  }
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(OTA_HOSTNAME);
  const PortalCfg &n = settings().net;
  WiFi.begin(n.ssid, n.pass);
  g_net.attempt++;
  g_net.wifi = g_net.attempt > 1 ? WifiPhase::Failed : WifiPhase::Connecting;
  g_net.nextRetryMs = millis() + WIFI_RETRY_MS;
  publishNet();
  Serial.printf("[net] joining \"%s\" (attempt %d)\n", n.ssid, g_net.attempt);
}

// Returns true while connected.
bool wifiEnsure() {
  static bool wasUp = false;
  if (WiFi.status() == WL_CONNECTED) {
    if (!wasUp) {
      wasUp = true;
      g_net.wifi = WifiPhase::Connected;
      g_net.attempt = 0;
      g_net.rssi = WiFi.RSSI();
      configTzTime(settingsTzPosix(), NTP_SERVER1, NTP_SERVER2);
      portENTER_CRITICAL(&g_refusalMux);
      g_refusal = SetupRefusal{};
      portEXIT_CRITICAL(&g_refusalMux);
      g_lastReason = 0;
      Serial.printf("[net] connected ip=%s rssi=%d\n", WiFi.localIP().toString().c_str(), g_net.rssi);
#if ENABLE_OTA
      ArduinoOTA.setHostname(OTA_HOSTNAME);
      ArduinoOTA.setPassword(OTA_PASSWORD);
      ArduinoOTA.setMdnsEnabled(false);   // mDNS costs ~5 KB + a task that TLS needs (v3); flash by IP
      ArduinoOTA.begin();
#endif
      publishNet();
    }
    return true;
  }
  if (g_net.lastReason != g_lastReason) {
    g_net.lastReason = g_lastReason;
    publishNet();
  }
  // Automatic portal (docs/12): the network is there but keeps refusing us, or no SSID at all.
  if (setupAutoEnter(settings().net.ssid[0] != '\0', g_lastReason, refusingMs())) {
    Serial.printf("[net] refused (reason %u) for %lu s: opening the setup portal\n", g_lastReason,
                  (unsigned long)(refusingMs() / 1000));
    settingsRequestSetup(true);
    delay(200);
    ESP.restart();
  }
  if (wasUp) {
    wasUp = false;
    Serial.println("[net] WiFi lost");
    g_net.attempt = 0;
    wifiBegin();
  } else if ((int32_t)(millis() - g_net.nextRetryMs) >= 0) {
    WiFi.disconnect(false);
    wifiBegin();
  }
  return false;
}

void checkTime() {
  if (g_net.timeSynced) return;
  if (time(nullptr) > 1700000000) {
    g_net.timeSynced = true;
    publishNet();
    Serial.println("[net] time synced");
  }
}

// Rain is plausible: a wet weather code now, or >= 10 % in the next 3 hours.
bool wetHint(const Weather &w) {
  RadarStatus r;                                   // the radar's own evidence: monsoon cells
  appGetRadar(r);                                  // often pop up unforecast (v3-R3 NTH 1)
  if (r.rain && r.rainMi <= 60) return true;
  if (!w.valid) return true;
  const WxKind k = wxKind(w.code);
  if (k == WxKind::Drizzle || k == WxKind::Rain || k == WxKind::Storm) return true;
  for (uint8_t i = 0; i < w.nHourly && i < 3; i++)
    if (w.hourly[i].pop >= 10) return true;
  return false;
}

// One adsbdb lookup per cycle, nearest aircraft first.
void lookupOneRoute() {
  for (uint8_t i = 0; i < g_traffic.n; i++) {
    const Aircraft &a = g_traffic.ac[i];
    if (a.distNm > LOOKUP_RADIUS_NM) break;          // list is sorted by distance
    if (a.onGround || !routeNeeded(a)) continue;
    if (!routeHasRoom(a, g_traffic)) return;         // cache full of nearer planes: no churn
    routeLookup(a);
    return;
  }
}

void netTask(void *) {
  memset(&g_net, 0, sizeof(g_net));
  strncpy(g_net.ssid, settings().net.ssid, sizeof(g_net.ssid) - 1);
  appTrafficLock();
  memset(&g_traffic, 0, sizeof(g_traffic));
  appTrafficUnlock();
  memset(&g_weather, 0, sizeof(g_weather));
  wifiBegin();

  uint32_t nextAdsb = 0, nextWx = 0;
  uint32_t adsbBackoff = 0;

  for (;;) {
#if ENABLE_OTA
    ArduinoOTA.handle();
#endif
    if (!wifiEnsure()) {
      todayService();                    // the log keeps saving through a WiFi outage (no network)
      vTaskDelay(pdMS_TO_TICKS(250));
      continue;
    }
    checkTime();
    const uint32_t now = millis();
    {   // on-screen diagnostics (weather screen while data is missing). 8-BIT RAM only: the
        // IRAM heap (~40 KB) is EXEC-only and useless to TLS, so it must not be counted.
      int code, tls;
      httpLastFailure(code, tls);
      g_net.lastHttp = (int16_t)code;
      g_net.lastTls = tls;
      g_net.heapFreeK = heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024;
      g_net.heapMinK = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT) / 1024;
      g_net.heapLargestK = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024;
      snprintf(g_net.wxErr, sizeof(g_net.wxErr), "%s", weatherLastError());
    }

    // Weather: every 10 min, 60 s retry on failure.
    if ((int32_t)(now - nextWx) >= 0) {
      const bool ok = weatherFetch(g_weather);
      if (ok) appSetWeather(g_weather);
      // A portal-saved location learns its elevation once, from Open-Meteo (docs/12).
      PortalCfg &n = settings().net;
      if (ok && n.locSaved && !n.elevKnown && g_weather.elevM > -500 && g_weather.elevM < 9000) {   // NaN fails
        n.elevFt = g_weather.elevM * 3.28084f;
        n.elevKnown = true;
        obsSetElevation(n.elevFt);
        settingsSavePortal();
        Serial.printf("[wx] saved location elevation %.0f ft\n", (double)n.elevFt);
      }
      g_net.weatherTried = true;
      publishNet();
      nextWx = millis() + (ok ? WEATHER_POLL_MS : WEATHER_RETRY_MS);
    }

    // ADS-B: 5 s on weather, 2 s on the plane card; exponential backoff on failure.
    if ((int32_t)(millis() - nextAdsb) >= 0) {
      const bool ok = adsbFetch(g_traffic);
      const bool planeUp = appGetUiScreen() == UiScreen::Plane;
      uint8_t radius;
      uint16_t planInterval;
      appGetPollPlan(radius, planInterval);
      uint32_t interval = planeUp ? ADSB_POLL_PLANE_MS : planInterval;
      if (ok) {
        adsbBackoff = 0;
      } else {
        adsbBackoff = adsbBackoff ? min<uint32_t>(adsbBackoff * 2, 60000) : 4000;
        interval = adsbBackoff;
      }
      appTrafficLock();
      g_traffic.nextRetryMs = millis() + interval;
      appTrafficUnlock();
      nextAdsb = g_traffic.nextRetryMs;
      appTrafficPublish();
      g_net.trafficTried = true;
      g_net.rssi = WiFi.RSSI();
      publishNet();
      if (ok) lookupOneRoute();
      g_radarOwesPoll = false;
      static uint32_t lastHeapLog = 0;
      if (millis() - lastHeapLog > 60000) {   // docs/03 -> Memory budget: keep >= 60 KB free
        lastHeapLog = millis();
        Serial.printf("[net] heap free=%u min=%u largest=%u\n", heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
                      heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        Serial.printf("[net] stack free net=%u loop=%u\n", (unsigned)uxTaskGetStackHighWaterMark(nullptr),
                      (unsigned)uxTaskGetStackHighWaterMark(xTaskGetHandle("loopTask")));
      }
    }

    // Today's Sky (docs/11): CSV lines, type lookups, the saved set and summary. Between
    // jobs: a radar convert is never in progress here.
    todayService();

    // Rain radar (docs/10 -> Scheduling, v3-R1-10): at most one frame per pass and an
    // ADS-B poll between any two frames (a 300 KB download would delay the card).
    // v3-R2-1: defer ONLY for what can delay a pop (card up, a plane will-pop or passing
    // the ENTER test) - NOT for nearby planes: Gilbert almost always has a trainer close -
    // and never for more than RADAR_DEFER_MAX_S in a row: then one frame runs anyway.
    if (!g_radarOwesPoll) {
      const UiScreen scr = appGetUiScreen();
      bool hot = scr == UiScreen::Plane;
      for (uint8_t i = 0; i < g_traffic.n && !hot; i++) {
        const Aircraft &a = g_traffic.ac[i];
        hot = mapQualifies(a.distNm, a.elDeg, a.altFt) || mapWillPopSecs(a);
      }
      const uint32_t nowMs = millis();
      const bool starved = g_radarDeferSince && nowMs - g_radarDeferSince > RADAR_DEFER_MAX_S * 1000UL;
      if (hot && !starved) {
        if (!g_radarDeferSince) g_radarDeferSince = nowMs;
      } else {
        if (g_radarDeferSince) g_radarDeferredMs += nowMs - g_radarDeferSince;
        g_radarDeferSince = 0;
        if (radarService(scr == UiScreen::Radar, wetHint(g_weather))) g_radarOwesPoll = true;
      }
      if (nowMs - g_radarDeferLogMs > 3600000UL) {                 // docs/10 device test
        Serial.printf("[radar] deferred %lu s in the last hour\n", (unsigned long)(g_radarDeferredMs / 1000));
        g_radarDeferredMs = 0;
        g_radarDeferLogMs = nowMs;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

}  // namespace

void netTaskStart() {
  // 16 KB stack: TLS handshakes are stack hungry; big buffers are static.
  xTaskCreatePinnedToCore(netTask, "net", 16384, nullptr, 1, nullptr, 0);
}
