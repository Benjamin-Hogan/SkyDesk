// byte_source.h - the minimal reader ArduinoJson needs (read / readBytes) plus
// peek, so streaming parsers are host-testable: the device wraps an HTTP
// stream (http_json.cpp), the host tests wrap a string.
#pragma once

#include <stddef.h>

class ByteSource {
 public:
  virtual ~ByteSource() = default;
  virtual int peek() = 0;                              // -1 on end / timeout
  virtual int read() = 0;                              // -1 on end / timeout
  virtual size_t readBytes(char *buf, size_t n) = 0;   // ArduinoJson custom reader API
};
