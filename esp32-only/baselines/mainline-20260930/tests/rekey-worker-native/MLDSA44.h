#pragma once
#include <atomic>
#include <cstring>
#include <freertos/task.h>
inline std::atomic<bool> holdCrypto{false};
inline bool failSign = false;
class MLDSA44 {
public:
  static int sign(uint8_t *out, size_t *length, const uint8_t *, size_t, const uint8_t *) {
    while (holdCrypto.load()) vTaskDelay(1);
    *length = 2420; memset(out, 0x55, 2420); return failSign ? -1 : 0;
  }
};
