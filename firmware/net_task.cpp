#include "net_task.h"
#include "adsb_client.h"
#include "app_state.h"
#include "route_client.h"
#include "weather_client.h"

#include <WiFi.h>
#if ENABLE_OTA
#include <ArduinoOTA.h>
#endif

namespace {

NetStatus g_net;
Traffic   g_traffic;   // net-side copy (static: too big for the task stack)
Weather   g_weather;

void publishNet() { appSetNet(g_net); }

void wifiBegin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(OTA_HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  g_net.attempt++;
  g_net.wifi = g_net.attempt > 1 ? WifiPhase::Failed : WifiPhase::Connecting;
  g_net.nextRetryMs = millis() + WIFI_RETRY_MS;
  publishNet();
  Serial.printf("[net] joining \"%s\" (attempt %d)\n", WIFI_SSID, g_net.attempt);
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
      configTzTime(NTP_TZ, NTP_SERVER1, NTP_SERVER2);
      Serial.printf("[net] connected ip=%s rssi=%d\n", WiFi.localIP().toString().c_str(), g_net.rssi);
#if ENABLE_OTA
      ArduinoOTA.setHostname(OTA_HOSTNAME);
      ArduinoOTA.setPassword(OTA_PASSWORD);
      ArduinoOTA.begin();
#endif
      publishNet();
    }
    return true;
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

// One adsbdb lookup per cycle, nearest aircraft first.
void lookupOneRoute() {
  for (uint8_t i = 0; i < g_traffic.n; i++) {
    const Aircraft &a = g_traffic.ac[i];
    if (a.distNm > LOOKUP_RADIUS_NM) break;          // list is sorted by distance
    if (a.onGround || !routeNeeded(a)) continue;
    routeLookup(a);
    return;
  }
}

void netTask(void *) {
  memset(&g_net, 0, sizeof(g_net));
  strncpy(g_net.ssid, WIFI_SSID, sizeof(g_net.ssid) - 1);
  memset(&g_traffic, 0, sizeof(g_traffic));
  memset(&g_weather, 0, sizeof(g_weather));
  wifiBegin();

  uint32_t nextAdsb = 0, nextWx = 0;
  uint32_t adsbBackoff = 0;

  for (;;) {
#if ENABLE_OTA
    ArduinoOTA.handle();
#endif
    if (!wifiEnsure()) {
      vTaskDelay(pdMS_TO_TICKS(250));
      continue;
    }
    checkTime();
    const uint32_t now = millis();

    // Weather: every 10 min, 60 s retry on failure.
    if ((int32_t)(now - nextWx) >= 0) {
      const bool ok = weatherFetch(g_weather);
      if (ok) appSetWeather(g_weather);
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
      g_traffic.nextRetryMs = millis() + interval;
      nextAdsb = g_traffic.nextRetryMs;
      appSetTraffic(g_traffic);
      g_net.radarTried = true;
      g_net.rssi = WiFi.RSSI();
      publishNet();
      if (ok) lookupOneRoute();
      static uint32_t lastHeapLog = 0;
      if (millis() - lastHeapLog > 60000) {   // docs/03 -> Memory budget: keep >= 60 KB free
        lastHeapLog = millis();
        Serial.printf("[net] heap free=%u min=%u largest=%u\n", ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                      ESP.getMaxAllocHeap());
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
