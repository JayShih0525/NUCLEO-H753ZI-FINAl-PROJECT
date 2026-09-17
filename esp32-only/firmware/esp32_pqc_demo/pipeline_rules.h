#pragma once
#include <stddef.h>
#include <stdint.h>

namespace pending_rekey {
// Pure envelope/state checks shared by the real command handler and native tests.
// Crypto MAC, worker phase and commit-token checks are additional requirements.
inline bool validRequest(uint8_t action, uint8_t capture, size_t dataLength,
                         uint32_t epoch, uint32_t count, uint32_t activeEpoch,
                         uint32_t activeCount, uint32_t limit, bool enabled) {
  constexpr size_t sizes[] = {0, 32, 0, 1088, 32};
  if (action > 4 || capture > 1 || dataLength != sizes[action] || epoch != activeEpoch ||
      count != activeCount || count > limit || limit < 3 || activeEpoch == UINT32_MAX) return false;
  if (!enabled && (action != 1 || count != 0)) return false;
  if (!capture && (count != limit || (action != 2 && action != 3))) return false;
  if (capture && count == limit && action != 4) return false;
  if (action == 4 && (!capture || count != limit)) return false;
  return true;
}
}
