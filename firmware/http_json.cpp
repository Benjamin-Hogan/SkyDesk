#include "http_json.h"
#include "config.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

int httpGetJson(const char *url, JsonDocument &doc, const JsonDocument &filter, bool &jsonOk) {
  jsonOk = false;
  WiFiClientSecure tls;
  tls.setInsecure();   // public read-only feeds, no secrets sent (docs/04)
  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.useHTTP10(true);  // no chunked encoding -> stream straight into ArduinoJson
  http.setReuse(false);
  if (!http.begin(tls, url)) return -1;
  http.setUserAgent(USER_AGENT);
  http.addHeader("Accept", "application/json");

  const int code = http.GET();
  if (code == 200) {
    DeserializationError err =
        deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    jsonOk = !err;
    if (err) Serial.printf("[http] JSON %s  %s\n", err.c_str(), url);
  } else {
    Serial.printf("[http] HTTP %d  %s\n", code, url);
  }
  http.end();
  return code;
}
