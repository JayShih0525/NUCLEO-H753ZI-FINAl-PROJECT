#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace demo_protocol {
// Writer must implement exact writes (including partial-write continuation).
// Coalescing changes write calls, never the length-prefixed byte stream.
template<class Writer>
bool writeFramedBytes(const uint8_t *data, size_t length, bool coalesce, Writer exact) {
  if ((length && !data) || length > UINT32_MAX) return false;
  uint8_t prefix[4] = {static_cast<uint8_t>(length >> 24),
      static_cast<uint8_t>(length >> 16), static_cast<uint8_t>(length >> 8),
      static_cast<uint8_t>(length)};
  if (coalesce && length <= 32) {
    uint8_t packet[36];
    memcpy(packet, prefix, 4);
    if (length) memcpy(packet + 4, data, length);
    return exact(packet, length + 4);
  }
  return exact(prefix, 4) && (!length || exact(data, length));
}
}
