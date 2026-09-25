// ==========================================================================
//  SkyDesk - weather by default, plane card when an aircraft is overhead.
//  Core 1 (this file): touch, tracker, screen state machine, drawing.
//  Core 0 (net_task.cpp): WiFi, NTP, all HTTP.
//  Docs: docs/01-product-spec.md (behaviour), docs/03-architecture.md (design)
// ==========================================================================
#include <Arduino.h>
#include <TFT_eSPI.h>

#include "app_state.h"
#include "config.h"
#include "geo.h"
#include "map_model.h"
#include "net_task.h"
#include "route_client.h"
#include "settings.h"
#include "touch_input.h"
#include "sd_store.h"
#include "radar_client.h"
#include "tracker.h"
#include "ui.h"

namespace {

TFT_eSPI tft;

// UI-side snapshots (static: too big for the loop task stack)
Traffic   g_traffic;
Weather   g_weather;
NetStatus g_net;

enum class Scr : uint8_t { Boot, Weather, Plane, Setup, Map, Radar };
Scr      g_scr = Scr::Boot;
Scr      g_home = Scr::Weather;   // where the plane card returns to: Weather, Map or Radar
// Idle (docs/09 M1b, v3-R3-2): TWO stamps. The timer restarts on a touch OR a return from
// a card; the cap counts from the last REAL touch only, so pops can't hold the screen.
uint32_t g_idleTimerMs = 0;
uint32_t g_lastTouchMs = 0;
char     g_cardHex[7] = "";       // the overhead plane on the card (M7: focused after it closes)
bool     g_cardAuto = false;      // the card popped by itself (not opened from the map strip)
uint32_t g_lastTick = 0;
uint32_t g_lastBoot = 0;
uint32_t g_arrivalMs = 0;
uint8_t  g_backlight = 0;

UiScreen toUi(Scr s) {
  switch (s) {
    case Scr::Weather: return UiScreen::Weather;
    case Scr::Plane:   return UiScreen::Plane;
    case Scr::Setup:   return UiScreen::Setup;
    case Scr::Map:     return UiScreen::Map;
    case Scr::Radar:   return UiScreen::Radar;
    default:           return UiScreen::Boot;
  }
}

void go(Scr next) {
  if (next == g_scr) return;
  Serial.printf("[ui] screen %d -> %d\n", (int)g_scr, (int)next);
  const bool toPlane = next == Scr::Plane;
  if (g_scr != Scr::Boot && next != Scr::Setup && g_scr != Scr::Setup) uiWipe(toPlane);
  if (g_scr == Scr::Map && next != Scr::Plane) mapLeave();   // the card returns to the map
  if (g_scr == Scr::Radar) radarLeave();                     // unmount SD: TLS needs the RAM
  const bool fromCard = g_scr == Scr::Plane;
  g_scr = next;
  if (next == Scr::Map || next == Scr::Radar) {
    g_idleTimerMs = millis();                              // the idle timer restarts ...
    if (!fromCard) g_lastTouchMs = g_idleTimerMs;          // ... the cap only on a real visit
  }
  appSetUiScreen(toUi(next));
  switch (next) {
    case Scr::Weather: weatherEnter(); break;
    case Scr::Plane:   planeEnter(); break;
    case Scr::Setup:   setupEnter(); break;
    case Scr::Map:
      mapEnter();
      if (fromCard && g_cardAuto) mapCardClosed(g_cardHex);   // M7
      break;
    case Scr::Radar:   radarEnter(); break;
    case Scr::Boot:    bootDraw(g_net, true); break;
  }
}

// Backlight: full by day, dimmed at night (from sunrise/sunset), full while a
// plane is shown (docs/06-ui-spec.md §6).
void updateBacklight() {
  uint8_t level = BL_FULL;
  if (settings().nightDim && g_weather.valid && g_scr != Scr::Plane && g_scr != Scr::Setup) {
    const time_t now = time(nullptr);
    const time_t pad = BL_NIGHT_PAD_MIN * 60;
    if (now > 1700000000 && (now < g_weather.sunrise[0] - pad || now > g_weather.sunset[0] + pad))
      level = BL_NIGHT;
  }
#if LDR_AUTO
  // LDR reads low in bright light on the CYD; cap brightness in a dark room.
  if (analogRead(LDR_PIN) > 3000 && level > BL_NIGHT) level = BL_NIGHT;
#endif
  if (level != g_backlight) {
    g_backlight = level;
    analogWrite(TFT_BL, level);
  }
}

// RGB LED (active-LOW): two amber pulses on arrival.
void updateLed(uint32_t now) {
  const uint32_t t = now - g_arrivalMs;
  const bool on = g_arrivalMs && (t < 200 || (t >= 400 && t < 600));
  static bool wasOn = false;
  if (on == wasOn) return;
  wasOn = on;
  analogWrite(LED_R_PIN, on ? 0 : 255);
  analogWrite(LED_G_PIN, on ? 200 : 255);
  digitalWrite(LED_B_PIN, HIGH);
}

void handleTouch(const TouchEvent &e, uint32_t now) {
  if (e.evt == TouchEvt::None) return;
  if (e.evt == TouchEvt::Hold) {           // 3 s hold: Settings (or calibration inside it)
    if (g_scr == Scr::Setup) setupStartCal();
    else go(Scr::Setup);
    return;
  }
  switch (g_scr) {
    case Scr::Setup:
      if (e.evt == TouchEvt::Tap && !setupInCalibration() && setupTouch(e.x, e.y) == SetupResult::Done)
        go(trackerView().mode != PlaneMode::None ? Scr::Plane : g_home);
      break;
    case Scr::Radar:
      g_idleTimerMs = g_lastTouchMs = now;
      if (e.evt == TouchEvt::Tap && radarTouchBack(e.x, e.y)) {
        g_home = Scr::Weather;
        go(Scr::Weather);
      }
      break;
    case Scr::Map: {
      g_idleTimerMs = g_lastTouchMs = now;
      if (e.evt != TouchEvt::Tap) break;
      char hex[7] = "";
      switch (mapTouch(g_traffic, e.x, e.y, hex)) {
        case MapAction::Back:
          g_home = Scr::Weather;
          go(Scr::Weather);
          break;
        case MapAction::OpenPlane:
          trackerForceHex(g_traffic, hex, now);   // card pops; it returns to the map
          break;
        case MapAction::None:
          break;
      }
      break;
    }
    case Scr::Plane:
      g_lastTouchMs = now;               // a touch on the card is a touch (idle cap, v3-R3-2)
      if (e.evt == TouchEvt::Tap) trackerTapNext();
      else if (e.evt == TouchEvt::LongPress) trackerDismiss(now);
      break;
    case Scr::Weather:
      if (e.evt == TouchEvt::Tap && e.y >= 210) {   // traffic chip -> plane map (docs/08)
        g_home = Scr::Map;
        go(Scr::Map);
      } else if (e.evt == TouchEvt::Tap && e.y >= 30 && e.y <= 112) {   // the hero band -> radar (docs/10)
        g_home = Scr::Radar;
        go(Scr::Radar);
      }
      break;
    case Scr::Boot:
      break;
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.printf("\n[boot] SkyDesk %s  obs %.4f,%.4f\n", FW_VERSION, (double)OBS_LAT, (double)OBS_LON);

  pinMode(LED_R_PIN, OUTPUT);
  pinMode(LED_G_PIN, OUTPUT);
  pinMode(LED_B_PIN, OUTPUT);
  digitalWrite(LED_R_PIN, HIGH);   // active-LOW: off
  digitalWrite(LED_G_PIN, HIGH);
  digitalWrite(LED_B_PIN, HIGH);

  tft.init();
  tft.setRotation(TFT_ROTATION);
  tft.fillScreen(COL_BG);

  settingsLoad();
  appStateInit();
  routeInit();
  uiInit(tft);          // sprites before WiFi: contiguous heap
  touchInit();
  sdInit();             // v3: own SPI bus (HSPI); absent card = radar disabled
  radarClientInit();    // index the radar frames already on the card
  geo::selfTest();

  appGetNet(g_net);
  bootDraw(g_net, true);
  updateBacklight();
  netTaskStart();
}

void loop() {
  if (millis() - g_lastTick < UI_TICK_MS) return;
  g_lastTick = millis();

  // Pull fresh snapshots (cheap copies under the app_state mutex).
  const bool newTraffic = appTrafficVersion() != g_traffic.version;
  if (newTraffic) appGetTraffic(g_traffic);
  if (appWeatherVersion() != g_weather.version) appGetWeather(g_weather);
  appGetNet(g_net);

  // Read the clock AFTER the snapshots: the net task stamps fetchedMs with its own
  // millis(), so a `now` taken earlier could be a few ms older than the data and
  // make unsigned "age" math wrap (seen on-device as the map bouncing to weather).
  const uint32_t now = millis();
  if (newTraffic) {
    appGetTraffic(g_traffic);
    if (g_traffic.ok) trailsUpdate(g_traffic, now);   // trails exist before the map opens
  }

  trackerUpdate(g_traffic, now);
  handleTouch(touchPoll(now), now);

  // Screen state machine (docs/03-architecture.md).
  switch (g_scr) {
    case Scr::Boot:
      if (g_net.wifi == WifiPhase::Connected && g_net.weatherTried) {
        go(Scr::Weather);
      } else if (now - g_lastBoot > 1000) {
        g_lastBoot = now;
        bootDraw(g_net, false);
      }
      break;
    case Scr::Map:
      // mapIdleExpired compares signed: go(Map) stamps the timers with millis(), which can
      // be a few ms AFTER this loop's `now` (unsigned math once bounced the map straight
      // back to weather on-device: 1 -> 4 -> 1 within 100 ms).
      if (mapIdleExpired(now, g_idleTimerMs, g_lastTouchMs, mapFocusWillPop())) {   // weather is home
        g_home = Scr::Weather;
        go(Scr::Weather);
        break;
      }
      go(trackerView().mode != PlaneMode::None ? Scr::Plane : g_home);
      break;
    case Scr::Radar:
      if (mapIdleExpired(now, g_idleTimerMs, g_lastTouchMs, false)) {   // same rule, no pause
        g_home = Scr::Weather;
        go(Scr::Weather);
        break;
      }
      // fall through
    case Scr::Weather:
    case Scr::Plane:
      go(trackerView().mode != PlaneMode::None ? Scr::Plane : g_home);
      break;
    case Scr::Setup:
      setupTick(now);
      break;
  }

  const bool arrival = trackerTakeArrival();
  if (arrival) g_arrivalMs = now;

  if (g_scr == Scr::Weather) {
    const bool trafficUp = g_traffic.failStreak < ADSB_FAILOVER_AFTER;
    weatherUpdate(g_weather, g_traffic, trafficUp, now);
  } else if (g_scr == Scr::Plane) {
    planeUpdate(trackerView(), g_traffic, now, arrival);
  } else if (g_scr == Scr::Map) {
    mapUpdate(g_traffic, g_traffic.failStreak < ADSB_FAILOVER_AFTER, now);
  } else if (g_scr == Scr::Radar) {
    radarUpdate(now);                  // acknowledges each frame list it takes
  }
  if (g_scr != Scr::Radar) appRadarAck(appRadarVersion());   // no frame file is open
  if (g_scr == Scr::Plane) {           // remember the card's plane for M7
    const TrackView &v = trackerView();
    snprintf(g_cardHex, sizeof(g_cardHex), "%s", v.ac.hex);
    g_cardAuto = v.mode == PlaneMode::Live || v.mode == PlaneMode::Departing;
  }

  updateBacklight();
  updateLed(now);
}
