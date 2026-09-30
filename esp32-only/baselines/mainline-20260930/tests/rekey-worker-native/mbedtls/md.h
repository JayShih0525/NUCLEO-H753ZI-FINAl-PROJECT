#pragma once
#include <cstddef>
#include <cstdint>
#define MBEDTLS_MD_SHA256 1
inline void *mbedtls_md_info_from_type(int) { return nullptr; }
// Deliberately a deterministic TEST DOUBLE, not cryptography. Python tests use
// real DSA/KEM/HMAC/GCM. This harness tests production worker lifecycle only.
inline int mbedtls_md_hmac(void *, const uint8_t *key, size_t k, const uint8_t *data, size_t n, uint8_t *out) {
  for (size_t i = 0; i < 32; ++i) out[i] = key[i % k];
  for (size_t i = 0; i < n; ++i) out[i % 32] ^= data[i];
  return 0;
}
