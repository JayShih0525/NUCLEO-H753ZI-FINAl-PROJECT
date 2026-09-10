#include "device_identity.h"
#include <Preferences.h>
#include <MLDSA44.h>
#include <mbedtls/md.h>
#include <string.h>

bool loadOrCreateDeviceIdentity(uint8_t *publicKey, uint8_t *secretKey) {
  Preferences storage;
  if (!storage.begin("pqc-identity", false)) return false;
  constexpr size_t PK = MLDSA44::PUBLIC_KEY_SIZE;
  constexpr size_t SK = MLDSA44::SECRET_KEY_SIZE;
  uint8_t blob[PK + SK];
  bool ok = false;
  if (storage.isKey("identity-v1")) {
    ok = storage.getBytesLength("identity-v1") == sizeof(blob) &&
         storage.getBytes("identity-v1", blob, sizeof(blob)) == sizeof(blob);
    if (ok) {
      memcpy(publicKey, blob, PK);
      memcpy(secretKey, blob + PK, SK);
    }
  } else if (MLDSA44::generateKeypair(publicKey, secretKey) == 0) {
    memcpy(blob, publicKey, PK);
    memcpy(blob + PK, secretKey, SK);
    ok = storage.putBytes("identity-v1", blob, sizeof(blob)) == sizeof(blob);
  }
  storage.end();
  volatile uint8_t *wipe = blob;
  for (size_t i = 0; i < sizeof(blob); ++i) wipe[i] = 0;
  // Validate the stored pair before announcing readiness.
  const uint8_t message[] = "identity-pair-check-v1";
  uint8_t signature[MLDSA44::SIGNATURE_SIZE];
  size_t length = 0;
  return ok && MLDSA44::sign(signature, &length, message, sizeof(message)-1, secretKey) == 0 &&
         length == sizeof(signature) &&
         MLDSA44::verify(signature, length, message, sizeof(message)-1, publicKey) == 0;
}

void printDeviceFingerprint(const uint8_t *publicKey) {
  uint8_t digest[32];
  if (mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), publicKey,
                 MLDSA44::PUBLIC_KEY_SIZE, digest) != 0) return;
  Serial.print("DEVICE_ID_SHA256 ");
  for (size_t i = 0; i < sizeof(digest); ++i) Serial.printf("%02x", digest[i]);
  Serial.println();
}
