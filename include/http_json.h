// http_json.h - one sequential HTTPS GET, streamed into ArduinoJson with a
// filter. Only ever called from the net task (one TLS session at a time).
#pragma once

#include <ArduinoJson.h>

// Returns the HTTP status (200 on success), or a negative HTTPClient error.
// On 200, `doc` holds the filtered document; `jsonOk` says whether it parsed.
int httpGetJson(const char *url, JsonDocument &doc, const JsonDocument &filter, bool &jsonOk);
