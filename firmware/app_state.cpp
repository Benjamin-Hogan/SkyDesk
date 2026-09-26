#include "app_state.h"

namespace {
SemaphoreHandle_t g_mtx = nullptr;
Traffic g_traffic;           // the ONE shared copy (net task writes it under the lock)
Weather g_weather;
NetStatus g_net;
volatile UiScreen g_screen = UiScreen::Boot;
volatile uint8_t  g_pollRadius = POLL_RADIUS_NM;
volatile uint16_t g_pollInterval = ADSB_POLL_WEATHER_MS;

struct Lock {
  Lock() { xSemaphoreTake(g_mtx, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(g_mtx); }
};
}  // namespace

void appStateInit() {
  g_mtx = xSemaphoreCreateMutex();
  memset(&g_traffic, 0, sizeof(g_traffic));
  memset(&g_weather, 0, sizeof(g_weather));
  memset(&g_net, 0, sizeof(g_net));
}

Traffic &appTrafficShared() { return g_traffic; }
void appTrafficLock() { xSemaphoreTake(g_mtx, portMAX_DELAY); }
void appTrafficUnlock() { xSemaphoreGive(g_mtx); }
void appTrafficPublish() { Lock l; g_traffic.version++; }

void appSetWeather(const Weather &w) {
  Lock l;
  const uint32_t v = g_weather.version + 1;
  g_weather = w;
  g_weather.version = v;
}

void appSetNet(const NetStatus &n) { Lock l; g_net = n; }

void appGetTraffic(Traffic &out) { Lock l; out = g_traffic; }
void appGetWeather(Weather &out) { Lock l; out = g_weather; }
void appGetNet(NetStatus &out) { Lock l; out = g_net; }
uint32_t appTrafficVersion() { Lock l; return g_traffic.version; }
uint32_t appWeatherVersion() { Lock l; return g_weather.version; }

void appSetUiScreen(UiScreen s) { g_screen = s; }

void appSetPollPlan(uint8_t radiusNm, uint16_t intervalMs) {
  g_pollRadius = radiusNm;
  g_pollInterval = intervalMs;
}

void appGetPollPlan(uint8_t &radiusNm, uint16_t &intervalMs) {
  radiusNm = g_pollRadius;
  intervalMs = g_pollInterval;
}
UiScreen appGetUiScreen() { return g_screen; }

namespace {
char g_pinned[7] = "";
}
void appSetPinnedHex(const char *hex) {
  Lock l;
  strncpy(g_pinned, hex ? hex : "", sizeof(g_pinned) - 1);
  g_pinned[sizeof(g_pinned) - 1] = '\0';
}
void appGetPinnedHex(char out[7]) {
  Lock l;
  memcpy(out, g_pinned, 7);
}

namespace {
RadarStatus g_radar{};
}
volatile uint32_t g_radarAck = 0;
uint32_t appSetRadar(const RadarStatus &r) {
  Lock l;
  const uint32_t v = g_radar.version + 1;
  g_radar = r;
  g_radar.version = v;
  return v;
}
void appRadarAck(uint32_t v) { g_radarAck = v; }
uint32_t appRadarAcked() { return g_radarAck; }
void appGetRadar(RadarStatus &out) { Lock l; out = g_radar; }
uint32_t appRadarVersion() { Lock l; return g_radar.version; }
