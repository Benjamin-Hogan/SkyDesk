#include "weather_client.h"
#include "http_json.h"

#include <WiFi.h>

#include <math.h>

namespace {
char g_wxErr[40] = "";
}
const char *weatherLastError() { return g_wxErr; }

bool weatherFetch(Weather &w) {
  char url[512];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,"
           "wind_speed_10m,wind_direction_10m,is_day"
           "&hourly=temperature_2m,weather_code,precipitation_probability,is_day"
           "&daily=temperature_2m_max,temperature_2m_min,sunrise,sunset"
           "&temperature_unit=fahrenheit&wind_speed_unit=mph&timezone=auto"
           "&timeformat=unixtime&forecast_days=2&forecast_hours=8",
           (double)OBS_LAT, (double)OBS_LON);

  JsonDocument filter;
  filter["current"] = true;
  filter["hourly"] = true;
  filter["daily"] = true;

  JsonDocument doc;
  bool jsonOk;
  const int code = httpGetJson(url, doc, filter, jsonOk);
  if (code != 200 || !jsonOk) {
    int c, tls;
    httpLastFailure(c, tls);
    if (code != 200) snprintf(g_wxErr, sizeof(g_wxErr), "wx http %d tls %d", code, tls);
    else snprintf(g_wxErr, sizeof(g_wxErr), "wx json %s", httpLastJsonError());
    Serial.printf("[wx] %s\n", g_wxErr);
    return false;
  }

  JsonObjectConst cur = doc["current"];
  if (cur.isNull() || cur["temperature_2m"].isNull()) {
    Serial.println("[wx] missing current block");
    snprintf(g_wxErr, sizeof(g_wxErr), "wx no current block");
    return false;
  }

  Weather n;
  memset(&n, 0, sizeof(n));
  n.tempF = (int16_t)lroundf(cur["temperature_2m"].as<float>());
  n.feelsF = (int16_t)lroundf(cur["apparent_temperature"] | (float)n.tempF);
  n.humidity = (uint8_t)constrain((int)(cur["relative_humidity_2m"] | 0), 0, 100);
  n.code = cur["weather_code"] | 0;
  n.isDay = (cur["is_day"] | 1) == 1;
  n.windMph = (uint16_t)lroundf(cur["wind_speed_10m"] | 0.0f);
  n.windDeg = cur["wind_direction_10m"] | 0;

  JsonObjectConst d = doc["daily"];
  n.hiF = (int16_t)lroundf(d["temperature_2m_max"][0] | (float)n.tempF);
  n.loF = (int16_t)lroundf(d["temperature_2m_min"][0] | (float)n.tempF);
  for (int i = 0; i < 2; i++) {
    n.sunrise[i] = d["sunrise"][i] | 0;
    n.sunset[i] = d["sunset"][i] | 0;
  }

  // Next WX_HOURS whole hours (skip the slot we're currently in).
  const time_t now = cur["time"] | (long)time(nullptr);
  JsonObjectConst h = doc["hourly"];
  JsonArrayConst ht = h["time"];
  for (size_t i = 0; i < ht.size() && n.nHourly < WX_HOURS; i++) {
    const time_t t = ht[i].as<long>();
    if (t <= now) continue;
    HourSlot &s = n.hourly[n.nHourly++];
    s.t = t;
    s.tempF = (int16_t)lroundf(h["temperature_2m"][i] | 0.0f);
    s.code = h["weather_code"][i] | 0;
    s.pop = h["precipitation_probability"][i] | 0;
    s.isDay = (h["is_day"][i] | 1) == 1;
  }

  n.valid = true;
  g_wxErr[0] = 0;
  n.fetchedEpoch = time(nullptr);
  n.fetchedMs = millis();
  w = n;
  Serial.printf("[wx] %dF code=%d hi=%d lo=%d hours=%d\n", n.tempF, n.code, n.hiF, n.loF, n.nHourly);
  return true;
}

WxKind wxKind(uint8_t c) {
  if (c == 0) return WxKind::Clear;
  if (c <= 2) return WxKind::Partly;
  if (c == 3) return WxKind::Cloud;
  if (c == 45 || c == 48) return WxKind::Fog;
  if (c >= 51 && c <= 57) return WxKind::Drizzle;
  if ((c >= 61 && c <= 67) || (c >= 80 && c <= 82)) return WxKind::Rain;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return WxKind::Snow;
  if (c >= 95) return WxKind::Storm;
  return WxKind::Cloud;
}

const char *wxLabel(uint8_t c, bool isDay) {
  switch (c) {
    case 0: return isDay ? "Sunny" : "Clear";
    case 1: return isDay ? "Mostly sunny" : "Mostly clear";
    case 2: return "Partly cloudy";
    case 3: return "Overcast";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Freezing rain";
    case 71: case 73: case 75: case 77: return "Snow";
    case 80: case 81: return "Showers";
    case 82: return "Heavy showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Storm, hail";
    default: return "Cloudy";
  }
}
