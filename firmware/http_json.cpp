#include "http_json.h"
#include <esp_heap_caps.h>
#include "config.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace {

// ByteSource over an HTTP body stream. Stream::peek()/read() are NON-blocking on
// ESP32 (they return -1 if the next TLS record hasn't arrived yet), so wait for
// data up to the HTTP timeout instead of mistaking a slow packet for the end.
class StreamSource : public ByteSource {
 public:
  explicit StreamSource(Stream &s) : s_(s) {}
  int peek() override { return waitData() ? s_.peek() : -1; }
  int read() override { return waitData() ? s_.read() : -1; }
  size_t readBytes(char *buf, size_t n) override {
    size_t got = 0;
    while (got < n && waitData()) got += s_.readBytes(buf + got, n - got);
    return got;
  }

 private:
  bool waitData() {
    const uint32_t start = millis();
    while (s_.available() <= 0) {
      if (millis() - start > HTTP_TIMEOUT_MS) return false;
      delay(2);   // net task only - never called from loop()
    }
    return true;
  }
  Stream &s_;
};

int g_failCode = 0, g_failTls = 0;

void noteFailure(int code, WiFiClientSecure &tls) {
  g_failCode = code;
  char msg[64] = "";                  // lastError() leaves it untouched when there is no error
  g_failTls = tls.lastError(msg, sizeof(msg));
  Serial.printf("[http] fail %d tls %d (%s) heap free=%u largest=%u\n", code, g_failTls, msg,
                heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

}  // namespace

const char *g_jsonErr = "";
const char *httpLastJsonError() { return g_jsonErr; }

void httpLastFailure(int &code, int &tls) {
  code = g_failCode;
  tls = g_failTls;
}

int httpGetStreamed(const char *url, bool (*fn)(ByteSource &src, void *ctx), void *ctx, bool &fnOk) {
  fnOk = false;
  WiFiClientSecure tls;
  tls.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.useHTTP10(true);   // no chunked encoding: the body is the raw JSON
  http.setReuse(false);
  if (!http.begin(tls, url)) { noteFailure(-1000, tls); return -1; }
  http.setUserAgent(USER_AGENT);
  http.addHeader("Accept", "application/json");
  const int code = http.GET();
  if (code == 200) {
    StreamSource src(*http.getStreamPtr());
    fnOk = fn(src, ctx);
  } else {
    Serial.printf("[http] HTTP %d  %s\n", code, url);
  }
  http.end();
  if (code != 200) noteFailure(code, tls);
  return code;
}

int httpGetJson(const char *url, JsonDocument &doc, const JsonDocument &filter, bool &jsonOk) {
  jsonOk = false;
  WiFiClientSecure tls;
  tls.setInsecure();   // public read-only feeds, no secrets sent (docs/04)
  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.useHTTP10(true);  // no chunked encoding -> stream straight into ArduinoJson
  http.setReuse(false);
  if (!http.begin(tls, url)) { noteFailure(-1000, tls); return -1; }
  http.setUserAgent(USER_AGENT);
  http.addHeader("Accept", "application/json");

  const int code = http.GET();
  if (code == 200) {
    DeserializationError err =
        deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    jsonOk = !err;
    g_jsonErr = err ? err.c_str() : "";
    if (err) Serial.printf("[http] JSON %s  %s\n", err.c_str(), url);
  } else {
    Serial.printf("[http] HTTP %d  %s\n", code, url);
  }
  http.end();
  if (code != 200) noteFailure(code, tls);
  return code;
}

int httpGetBody(const char *url, bool (*sink)(const uint8_t *buf, size_t n, void *ctx), void *ctx,
                size_t maxBytes, size_t &got, size_t &expected) {
  got = expected = 0;
  WiFiClientSecure tls;
  tls.setInsecure();   // public read-only data, no secrets sent (docs/04)
  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.useHTTP10(true);
  http.setReuse(false);
  if (!http.begin(tls, url)) { noteFailure(-1000, tls); return -1; }
  http.setUserAgent(USER_AGENT);
  const int code = http.GET();
  if (code != 200) {
    Serial.printf("[http] HTTP %d  %s\n", code, url);
    http.end();
    noteFailure(code, tls);
    return code;
  }
  // IEM answers HTTP/1.0 requests with NO Content-Length (Connection: close) - verified
  // live (reviewer C1). Then the body runs until the server closes; the caller proves
  // completeness from the content itself (TIFF strips).
  const int len = http.getSize();
  if (len > 0 && (size_t)len > maxBytes) {
    Serial.printf("[http] body size %d over the %u limit\n", len, (unsigned)maxBytes);
    http.end();
    noteFailure(HTTP_BODY_TOO_BIG, tls);
    return HTTP_BODY_TOO_BIG;
  }
  expected = len > 0 ? (size_t)len : 0;
  const size_t limit = len > 0 ? (size_t)len : maxBytes;
  WiFiClient *s = http.getStreamPtr();
  uint8_t buf[512];           // on the net task's stack: no permanent RAM
  uint32_t lastData = millis();
  while (got < limit) {
    const int avail = s->available();
    if (avail <= 0) {
      if (!s->connected()) break;                    // the server closed: end of the body
      if (millis() - lastData > HTTP_TIMEOUT_MS) break;
      delay(2);
      continue;
    }
    const size_t want = min<size_t>(min<size_t>((size_t)avail, sizeof(buf)), limit - got);
    const int n = s->readBytes(buf, want);
    if (n <= 0) continue;
    lastData = millis();
    if (!sink(buf, (size_t)n, ctx)) break;
    got += (size_t)n;
  }
  http.end();
  return code;
}
