#pragma once
#include <stddef.h>
#include <stdint.h>

// Authenticated byte records; AES-GCM image encryption remains independent.
namespace secure_records {
constexpr size_t MAX_PAYLOAD = 4096, HEADER = 32, TAG = 32;
void reset();
bool active();
bool start(const uint8_t secret[32], const uint8_t bindingHash[32], uint32_t epoch);
bool encode(uint8_t *packet, const uint8_t *data, size_t length);
bool inspect(const uint8_t *header, size_t &length);
bool verify(const uint8_t *packet, size_t length);
}
