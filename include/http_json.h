// http_json.h - one sequential HTTPS GET, streamed into ArduinoJson with a
// filter. Only ever called from the net task (one TLS session at a time).
#pragma once

#include <ArduinoJson.h>

// Returns the HTTP status (200 on success), or a negative HTTPClient error.
// On 200, `doc` holds the filtered document; `jsonOk` says whether it parsed.
int httpGetJson(const char *url, JsonDocument &doc, const JsonDocument &filter, bool &jsonOk);

#include "byte_source.h"
#include "config.h"
// Streamed variant: on HTTP 200, `fn(src, ctx)` consumes the body through a
// ByteSource (timed peek/read). Returns the HTTP status; `fnOk` = fn's result.
int httpGetStreamed(const char *url, bool (*fn)(ByteSource &src, void *ctx), void *ctx, bool &fnOk);

// The last failure (HTTP status or negative HTTPClient error, and the mbedTLS error if
// any) - for on-screen diagnostics. Net task only.
void httpLastFailure(int &code, int &tls);
// The last JSON parse error of httpGetJson ("" if it parsed), e.g. "NoMemory".
const char *httpLastJsonError();

// Raw body variant (radar frames -> SD): on HTTP 200, streams the body through `sink` in
// <= 512 B chunks. With a Content-Length, exactly that many bytes (`expected`); without
// one (IEM over HTTP/1.0), until the server closes, capped at maxBytes (`expected` = 0:
// the caller must prove completeness from the content). `got` = bytes delivered.
// Returns the HTTP status, a negative HTTPClient error, or HTTP_BODY_TOO_BIG.
#define HTTP_BODY_TOO_BIG (-3000)
int httpGetBody(const char *url, bool (*sink)(const uint8_t *buf, size_t n, void *ctx), void *ctx,
                size_t maxBytes, size_t &got, size_t &expected,
                uint32_t timeoutMs = HTTP_TIMEOUT_MS);
