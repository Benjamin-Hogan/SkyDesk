#include "route_client.h"
#include "http_json.h"


namespace {

struct Entry {
  RouteInfo info;
  uint32_t fetchedMs;
  uint32_t usedMs;
  bool used;
};

Entry g_cache[ROUTE_CACHE_N];
SemaphoreHandle_t g_mtx = nullptr;

struct Lock {
  Lock() { xSemaphoreTake(g_mtx, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(g_mtx); }
};

void copyStr(char *dst, size_t n, const char *src) {
  strncpy(dst, src ? src : "", n - 1);
  dst[n - 1] = '\0';
}

Entry *findLocked(const char *hex) {
  for (auto &e : g_cache)
    if (e.used && strcmp(e.info.hex, hex) == 0) return &e;
  return nullptr;
}

Entry *slotLocked(const char *hex) {
  if (Entry *e = findLocked(hex)) return e;
  Entry *victim = &g_cache[0];
  for (auto &e : g_cache) {
    if (!e.used) return &e;
    if (e.usedMs < victim->usedMs) victim = &e;   // least recently used
  }
  return victim;
}

void buildFilter(JsonDocument &f) {
  JsonObject ac = f["response"]["aircraft"].to<JsonObject>();
  for (const char *k : {"icao_type", "manufacturer", "registered_owner"}) ac[k] = true;
  JsonObject fr = f["response"]["flightroute"].to<JsonObject>();
  fr["callsign_iata"] = true;
  fr["airline"]["name"] = true;
  for (const char *end : {"origin", "destination"}) {
    JsonObject o = fr[end].to<JsonObject>();
    for (const char *k : {"iata_code", "municipality", "latitude", "longitude"}) o[k] = true;
  }
}

}  // namespace

void routeInit() {
  g_mtx = xSemaphoreCreateMutex();
  memset(g_cache, 0, sizeof(g_cache));
}

bool routeNeeded(const Aircraft &a) {
  Lock l;
  const Entry *e = findLocked(a.hex);
  if (!e) return true;
  if (strcmp(e->info.callsign, a.callsign) != 0) return true;   // new flight
  return millis() - e->fetchedMs > ROUTE_TTL_MS;
}

void routeLookup(const Aircraft &a) {
  char url[96];
  int n = snprintf(url, sizeof(url), ADSBDB_URL, a.hex);
  if (a.callsign[0]) snprintf(url + n, sizeof(url) - n, "?callsign=%s", a.callsign);

  JsonDocument filter;
  buildFilter(filter);
  JsonDocument doc;
  bool jsonOk;
  int code = httpGetJson(url, doc, filter, jsonOk);
  // adsbdb 404s the WHOLE request when only the callsign is unknown
  // ({"response":"unknown callsign"}) - retry for the aircraft alone.
  if (code == 404 && a.callsign[0]) {
    url[n] = '\0';
    doc.clear();
    code = httpGetJson(url, doc, filter, jsonOk);
  }
  // 404 = unknown aircraft: cache the miss so we don't ask again.
  if (!(code == 200 && jsonOk) && code != 404) return;

  RouteInfo r;
  memset(&r, 0, sizeof(r));
  copyStr(r.hex, sizeof(r.hex), a.hex);
  copyStr(r.callsign, sizeof(r.callsign), a.callsign);

  // "response" is a string ("unknown aircraft") when not found.
  JsonObjectConst resp = doc["response"].as<JsonObjectConst>();
  if (!resp.isNull()) {
    JsonObjectConst ac = resp["aircraft"];
    if (!ac.isNull()) {
      r.found = true;
      copyStr(r.icaoType, sizeof(r.icaoType), ac["icao_type"] | "");
      copyStr(r.manufacturer, sizeof(r.manufacturer), ac["manufacturer"] | "");
      copyStr(r.owner, sizeof(r.owner), ac["registered_owner"] | "");
    }
    JsonObjectConst fr = resp["flightroute"];
    if (!fr.isNull() && !fr["origin"].isNull() && !fr["destination"].isNull()) {
      r.hasRoute = true;
      copyStr(r.airline, sizeof(r.airline), fr["airline"]["name"] | "");
      copyStr(r.flightIata, sizeof(r.flightIata), fr["callsign_iata"] | "");
      copyStr(r.origIata, sizeof(r.origIata), fr["origin"]["iata_code"] | "");
      copyStr(r.destIata, sizeof(r.destIata), fr["destination"]["iata_code"] | "");
      copyStr(r.origCity, sizeof(r.origCity), fr["origin"]["municipality"] | "");
      copyStr(r.destCity, sizeof(r.destCity), fr["destination"]["municipality"] | "");
      r.oLat = fr["origin"]["latitude"] | 0.0;
      r.oLon = fr["origin"]["longitude"] | 0.0;
      r.dLat = fr["destination"]["latitude"] | 0.0;
      r.dLon = fr["destination"]["longitude"] | 0.0;
      if (!r.origIata[0] || !r.destIata[0]) r.hasRoute = false;
    }
  }

  Serial.printf("[route] %s %s -> %s %s%s%s\n", a.hex, a.callsign, r.found ? "found" : "unknown",
                r.hasRoute ? r.origIata : "", r.hasRoute ? ">" : "", r.hasRoute ? r.destIata : "");
  Lock l;
  Entry *e = slotLocked(a.hex);
  e->info = r;
  e->fetchedMs = millis();
  e->usedMs = millis();
  e->used = true;
}

bool routeGet(const char *hex, RouteInfo &out) {
  Lock l;
  Entry *e = findLocked(hex);
  if (!e) return false;
  e->usedMs = millis();
  out = e->info;
  return true;
}
