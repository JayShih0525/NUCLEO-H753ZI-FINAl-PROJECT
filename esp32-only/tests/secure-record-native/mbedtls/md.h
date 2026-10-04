#pragma once
// Windows-native test adapter only. Use real OS SHA-256/HMAC to cross-check
// production record code against Python. ESP32 uses its bundled mbedTLS.
#include <windows.h>
#include <bcrypt.h>
#include <cstddef>
#include <cstdint>
#include <vector>
#define MBEDTLS_MD_SHA256 1
inline void *mbedtls_md_info_from_type(int) { return nullptr; }
inline int mbedtls_md_hmac(void *, const uint8_t *key, size_t k, const uint8_t *data, size_t n, uint8_t *out) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  ULONG size = 0, copied = 0;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG)) return -1;
  NTSTATUS result = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size), sizeof(size), &copied, 0);
  std::vector<uint8_t> object(size);
  if (!result) result = BCryptCreateHash(algorithm, &hash, object.data(), size, const_cast<PUCHAR>(key), k, 0);
  if (!result) result = BCryptHashData(hash, const_cast<PUCHAR>(data), n, 0);
  if (!result) result = BCryptFinishHash(hash, out, 32, 0);
  if (hash) BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  return result ? -1 : 0;
}
