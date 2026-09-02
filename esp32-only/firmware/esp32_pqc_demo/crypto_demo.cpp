#include "crypto_demo.h"

#include <Arduino.h>
#include <MLDSA44.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/gcm.h>

#include "protocol.h"
#include "camera_demo.h"

extern "C" {
#include "src/mlkem768/api.h"
}

namespace {

constexpr size_t KEM_PUBLIC_KEY_SIZE = PQCLEAN_MLKEM768_CLEAN_CRYPTO_PUBLICKEYBYTES;
constexpr size_t KEM_SECRET_KEY_SIZE = PQCLEAN_MLKEM768_CLEAN_CRYPTO_SECRETKEYBYTES;
constexpr size_t KEM_CIPHERTEXT_SIZE = PQCLEAN_MLKEM768_CLEAN_CRYPTO_CIPHERTEXTBYTES;
constexpr size_t SHARED_SECRET_SIZE = PQCLEAN_MLKEM768_CLEAN_CRYPTO_BYTES;
constexpr size_t AES_NONCE_SIZE = 12;
constexpr size_t AES_TAG_SIZE = 16;
constexpr size_t MAX_MESSAGE_SIZE = 4096;
constexpr size_t MAX_AAD_SIZE = 256;
constexpr uint32_t DEFAULT_REKEY_INTERVAL = 10;
constexpr uint32_t MAX_REKEY_INTERVAL = 100000;
constexpr size_t CAMERA_METADATA_SIZE = 20;
constexpr size_t MAX_CAMERA_JPEG_SIZE = 1024 * 1024;

static_assert(KEM_PUBLIC_KEY_SIZE == 1184, "Unexpected ML-KEM-768 public key size");
static_assert(KEM_SECRET_KEY_SIZE == 2400, "Unexpected ML-KEM-768 secret key size");
static_assert(KEM_CIPHERTEXT_SIZE == 1088, "Unexpected ML-KEM-768 ciphertext size");
static_assert(SHARED_SECRET_SIZE == 32, "Unexpected ML-KEM shared secret size");
static_assert(MLDSA44::PUBLIC_KEY_SIZE == 1312, "Unexpected ML-DSA-44 public key size");
static_assert(MLDSA44::SIGNATURE_SIZE == 2420, "Unexpected ML-DSA-44 signature size");

uint8_t g_kemPublicKey[KEM_PUBLIC_KEY_SIZE];
uint8_t g_kemSecretKey[KEM_SECRET_KEY_SIZE];
uint8_t g_kemCiphertext[KEM_CIPHERTEXT_SIZE];
uint8_t g_sharedSecret[SHARED_SECRET_SIZE];
uint8_t g_sharedSecretCheck[SHARED_SECRET_SIZE];

uint8_t g_dsaPublicKey[MLDSA44::PUBLIC_KEY_SIZE];
uint8_t g_dsaSecretKey[MLDSA44::SECRET_KEY_SIZE];
uint8_t g_signature[MLDSA44::SIGNATURE_SIZE];

uint8_t g_plaintext[MAX_MESSAGE_SIZE];
uint8_t g_ciphertext[MAX_MESSAGE_SIZE];
uint8_t g_aad[MAX_AAD_SIZE];
uint8_t g_nonce[AES_NONCE_SIZE];
uint8_t g_tag[AES_TAG_SIZE];

uint8_t g_noncePrefix[4];
uint64_t g_nonceCounter = 1;
bool g_sessionReady = false;
bool g_kemKeyReady = false;
uint32_t g_rekeyInterval = DEFAULT_REKEY_INTERVAL;
uint32_t g_messageCount = 0;
uint32_t g_sessionEpoch = 0;
uint32_t g_cameraFrameId = 0;

void secureZero(void *pointer, size_t length) {
  volatile uint8_t *bytes = static_cast<volatile uint8_t *>(pointer);
  while (length-- > 0) {
    *bytes++ = 0;
  }
}

void nextNonce(uint8_t nonce[AES_NONCE_SIZE]) {
  memcpy(nonce, g_noncePrefix, sizeof(g_noncePrefix));
  const uint64_t counter = g_nonceCounter++;
  for (size_t i = 0; i < 8; ++i) {
    nonce[4 + i] = static_cast<uint8_t>(counter >> (56 - i * 8));
  }
}

void writeU16Be(uint8_t *output, uint16_t value) {
  output[0] = static_cast<uint8_t>(value >> 8);
  output[1] = static_cast<uint8_t>(value);
}

void writeU32Be(uint8_t *output, uint32_t value) {
  output[0] = static_cast<uint8_t>(value >> 24);
  output[1] = static_cast<uint8_t>(value >> 16);
  output[2] = static_cast<uint8_t>(value >> 8);
  output[3] = static_cast<uint8_t>(value);
}

int aesEncrypt(const uint8_t key[SHARED_SECRET_SIZE],
               const uint8_t nonce[AES_NONCE_SIZE],
               const uint8_t *aad,
               size_t aadLength,
               const uint8_t *plaintext,
               size_t plaintextLength,
               uint8_t *ciphertext,
               uint8_t tag[AES_TAG_SIZE]) {
  mbedtls_gcm_context context;
  mbedtls_gcm_init(&context);

  int result = mbedtls_gcm_setkey(
      &context, MBEDTLS_CIPHER_ID_AES, key, SHARED_SECRET_SIZE * 8);
  if (result == 0) {
    result = mbedtls_gcm_crypt_and_tag(
        &context,
        MBEDTLS_GCM_ENCRYPT,
        plaintextLength,
        nonce,
        AES_NONCE_SIZE,
        aad,
        aadLength,
        plaintext,
        ciphertext,
        AES_TAG_SIZE,
        tag);
  }

  mbedtls_gcm_free(&context);
  return result;
}

int aesDecrypt(const uint8_t key[SHARED_SECRET_SIZE],
               const uint8_t nonce[AES_NONCE_SIZE],
               const uint8_t *aad,
               size_t aadLength,
               const uint8_t *ciphertext,
               size_t ciphertextLength,
               const uint8_t tag[AES_TAG_SIZE],
               uint8_t *plaintext) {
  mbedtls_gcm_context context;
  mbedtls_gcm_init(&context);

  int result = mbedtls_gcm_setkey(
      &context, MBEDTLS_CIPHER_ID_AES, key, SHARED_SECRET_SIZE * 8);
  if (result == 0) {
    result = mbedtls_gcm_auth_decrypt(
        &context,
        ciphertextLength,
        nonce,
        AES_NONCE_SIZE,
        aad,
        aadLength,
        tag,
        AES_TAG_SIZE,
        ciphertext,
        plaintext);
  }

  mbedtls_gcm_free(&context);
  return result;
}

void sendError(const char *reason) {
  Serial.print("ERR ");
  Serial.println(reason);
  Serial.flush();
}

bool generateKemKeypair() {
  secureZero(g_kemPublicKey, sizeof(g_kemPublicKey));
  secureZero(g_kemSecretKey, sizeof(g_kemSecretKey));
  g_kemKeyReady =
      PQCLEAN_MLKEM768_CLEAN_crypto_kem_keypair(
          g_kemPublicKey, g_kemSecretKey) == 0;
  return g_kemKeyReady;
}

void clearSessionSecret() {
  secureZero(g_sharedSecret, sizeof(g_sharedSecret));
  secureZero(g_sharedSecretCheck, sizeof(g_sharedSecretCheck));
  g_sessionReady = false;
  g_messageCount = 0;
}

bool rotateKemKeypair() {
  clearSessionSecret();
  return generateKemKeypair();
}

bool recordSuccessfulMessage() {
  ++g_messageCount;
  return g_messageCount >= g_rekeyInterval;
}

void writeAesSuccess(bool rekeyRequired) {
  Serial.printf(
      "OK epoch=%lu count=%lu rekey=%u\n",
      static_cast<unsigned long>(g_sessionEpoch),
      static_cast<unsigned long>(g_messageCount),
      rekeyRequired ? 1 : 0);
  Serial.flush();
}

void finishMessage(bool rekeyRequired) {
  if (rekeyRequired) {
    rotateKemKeypair();
  }
}

void handleInfo() {
  Serial.printf(
      "INFO proto=3 kem=ML-KEM-768 aes=AES-256-GCM dsa=ML-DSA-44 "
      "camera=OV2640 kem_pk=1184 kem_ct=1088 dsa_pk=1312 dsa_sig=2420 "
      "rekey_every=%lu\n",
      static_cast<unsigned long>(g_rekeyInterval));
  Serial.flush();
}

void handleMemoryInfo() {
  constexpr uint32_t INTERNAL_CAPS = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

  const size_t internalFree = heap_caps_get_free_size(INTERNAL_CAPS);
  const size_t internalMinimum =
      heap_caps_get_minimum_free_size(INTERNAL_CAPS);
  const size_t internalLargest =
      heap_caps_get_largest_free_block(INTERNAL_CAPS);
  const size_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  const size_t psramMinimum =
      heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
  const size_t psramLargest =
      heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  const UBaseType_t pqcStackMinimum = uxTaskGetStackHighWaterMark(nullptr);

  Serial.printf(
      "MEM internal_free=%u internal_min=%u internal_largest=%u "
      "psram_free=%u psram_min=%u psram_largest=%u pqc_stack_min=%u\n",
      static_cast<unsigned int>(internalFree),
      static_cast<unsigned int>(internalMinimum),
      static_cast<unsigned int>(internalLargest),
      static_cast<unsigned int>(psramFree),
      static_cast<unsigned int>(psramMinimum),
      static_cast<unsigned int>(psramLargest),
      static_cast<unsigned int>(pqcStackMinimum));
  Serial.flush();
}

void handleCameraInfo() {
  Serial.printf(
      "CAMERA ready=%u mode=%s max_jpeg=%u frame_id=%lu\n",
      cameraIsReady() ? 1 : 0,
      cameraModeName(),
      static_cast<unsigned int>(MAX_CAMERA_JPEG_SIZE),
      static_cast<unsigned long>(g_cameraFrameId));
  Serial.flush();
}

void handleCameraMode(bool photoMode) {
  const bool ok = photoMode ? setCameraPhotoMode() : setCameraStreamMode();
  if (!ok) {
    sendError("CAMERA_MODE_FAILED");
    return;
  }
  Serial.printf("OK camera_mode=%s\n", cameraModeName());
  Serial.flush();
}

void handleCameraCaptureEncrypted() {
  if (!g_sessionReady) {
    sendError("NO_SESSION");
    return;
  }
  if (!cameraIsReady()) {
    sendError("CAMERA_NOT_READY");
    return;
  }

  const uint32_t started = millis();
  camera_fb_t *frame = captureCameraFrame();
  if (frame == nullptr || frame->buf == nullptr || frame->len == 0 ||
      frame->len > MAX_CAMERA_JPEG_SIZE || frame->format != PIXFORMAT_JPEG) {
    releaseCameraFrame(frame);
    sendError("CAMERA_CAPTURE_FAILED");
    return;
  }

  uint8_t *encrypted = static_cast<uint8_t *>(heap_caps_malloc(
      frame->len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (encrypted == nullptr) {
    releaseCameraFrame(frame);
    sendError("CAMERA_CIPHERTEXT_ALLOC_FAILED");
    return;
  }

  uint8_t metadata[CAMERA_METADATA_SIZE] = {
      'C', 'A', 'M', '2', 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  const uint32_t frameId = g_cameraFrameId++;
  writeU32Be(metadata + 4, g_sessionEpoch);
  writeU32Be(metadata + 8, frameId);
  writeU16Be(metadata + 12, static_cast<uint16_t>(frame->width));
  writeU16Be(metadata + 14, static_cast<uint16_t>(frame->height));
  writeU32Be(metadata + 16, static_cast<uint32_t>(frame->len));

  nextNonce(g_nonce);
  if (aesEncrypt(
          g_sharedSecret,
          g_nonce,
          metadata,
          sizeof(metadata),
          frame->buf,
          frame->len,
          encrypted,
          g_tag) != 0) {
    secureZero(encrypted, frame->len);
    heap_caps_free(encrypted);
    releaseCameraFrame(frame);
    sendError("CAMERA_AES_ENCRYPT_FAILED");
    return;
  }

  const size_t jpegLength = frame->len;
  releaseCameraFrame(frame);
  const bool rekeyRequired = recordSuccessfulMessage();
  Serial.printf(
      "OK epoch=%lu count=%lu rekey=%u frame=%lu jpeg=%u elapsed_ms=%lu\n",
      static_cast<unsigned long>(g_sessionEpoch),
      static_cast<unsigned long>(g_messageCount),
      rekeyRequired ? 1 : 0,
      static_cast<unsigned long>(frameId),
      static_cast<unsigned int>(jpegLength),
      static_cast<unsigned long>(millis() - started));
  Serial.flush();
  demo_protocol::writeFrame(metadata, sizeof(metadata));
  demo_protocol::writeFrame(g_nonce, sizeof(g_nonce));
  demo_protocol::writeFrame(encrypted, jpegLength);
  demo_protocol::writeFrame(g_tag, sizeof(g_tag));

  secureZero(encrypted, jpegLength);
  heap_caps_free(encrypted);
  finishMessage(rekeyRequired);
}

void handleSetRekeyInterval(const char *command) {
  constexpr char PREFIX[] = "SET_REKEY_INTERVAL ";
  const char *valueText = command + sizeof(PREFIX) - 1;
  char *end = nullptr;
  const unsigned long value = strtoul(valueText, &end, 10);

  if (end == valueText || *end != '\0' || value < 1 ||
      value > MAX_REKEY_INTERVAL) {
    sendError("BAD_REKEY_INTERVAL");
    return;
  }
  if (g_sessionReady) {
    sendError("SESSION_ACTIVE");
    return;
  }

  g_rekeyInterval = static_cast<uint32_t>(value);
  g_messageCount = 0;
  Serial.printf("OK rekey_every=%lu\n", value);
  Serial.flush();
}

void handleKemDecapsulate() {
  demo_protocol::writeLine("READY");

  size_t ciphertextLength = 0;
  if (!demo_protocol::readFrame(
          g_kemCiphertext, sizeof(g_kemCiphertext), ciphertextLength) ||
      ciphertextLength != KEM_CIPHERTEXT_SIZE) {
    sendError("BAD_KEM_CIPHERTEXT");
    return;
  }

  const uint32_t started = millis();
  if (PQCLEAN_MLKEM768_CLEAN_crypto_kem_dec(
          g_sharedSecret, g_kemCiphertext, g_kemSecretKey) != 0) {
    sendError("KEM_DECAP_FAILED");
    return;
  }

  g_sessionReady = true;
  g_messageCount = 0;
  ++g_sessionEpoch;
  Serial.printf(
      "KEM_OK epoch=%lu limit=%lu elapsed_ms=%lu\n",
      static_cast<unsigned long>(g_sessionEpoch),
      static_cast<unsigned long>(g_rekeyInterval),
      static_cast<unsigned long>(millis() - started));
  Serial.flush();
}

void handleAesDecrypt() {
  if (!g_sessionReady) {
    sendError("NO_SESSION");
    return;
  }

  demo_protocol::writeLine("READY");

  size_t nonceLength = 0;
  size_t aadLength = 0;
  size_t ciphertextLength = 0;
  size_t tagLength = 0;

  if (!demo_protocol::readFrame(g_nonce, sizeof(g_nonce), nonceLength) ||
      !demo_protocol::readFrame(g_aad, sizeof(g_aad), aadLength) ||
      !demo_protocol::readFrame(g_ciphertext, sizeof(g_ciphertext), ciphertextLength) ||
      !demo_protocol::readFrame(g_tag, sizeof(g_tag), tagLength) ||
      nonceLength != AES_NONCE_SIZE || tagLength != AES_TAG_SIZE) {
    sendError("BAD_AES_FRAME");
    return;
  }

  if (aesDecrypt(
          g_sharedSecret,
          g_nonce,
          g_aad,
          aadLength,
          g_ciphertext,
          ciphertextLength,
          g_tag,
          g_plaintext) != 0) {
    secureZero(g_plaintext, ciphertextLength);
    sendError("AES_AUTH_FAILED");
    return;
  }

  const bool rekeyRequired = recordSuccessfulMessage();
  writeAesSuccess(rekeyRequired);
  demo_protocol::writeFrame(g_plaintext, ciphertextLength);
  secureZero(g_plaintext, ciphertextLength);
  finishMessage(rekeyRequired);
}

void handleAesEncrypt() {
  if (!g_sessionReady) {
    sendError("NO_SESSION");
    return;
  }

  demo_protocol::writeLine("READY");

  size_t plaintextLength = 0;
  size_t aadLength = 0;
  if (!demo_protocol::readFrame(g_plaintext, sizeof(g_plaintext), plaintextLength) ||
      !demo_protocol::readFrame(g_aad, sizeof(g_aad), aadLength)) {
    sendError("BAD_AES_FRAME");
    return;
  }

  nextNonce(g_nonce);
  if (aesEncrypt(
          g_sharedSecret,
          g_nonce,
          g_aad,
          aadLength,
          g_plaintext,
          plaintextLength,
          g_ciphertext,
          g_tag) != 0) {
    sendError("AES_ENCRYPT_FAILED");
    return;
  }

  const bool rekeyRequired = recordSuccessfulMessage();
  writeAesSuccess(rekeyRequired);
  demo_protocol::writeFrame(g_nonce, sizeof(g_nonce));
  demo_protocol::writeFrame(g_ciphertext, plaintextLength);
  demo_protocol::writeFrame(g_tag, sizeof(g_tag));
  secureZero(g_plaintext, plaintextLength);
  finishMessage(rekeyRequired);
}

void handleDsaSign() {
  demo_protocol::writeLine("READY");

  size_t messageLength = 0;
  if (!demo_protocol::readFrame(g_plaintext, sizeof(g_plaintext), messageLength)) {
    sendError("BAD_DSA_MESSAGE");
    return;
  }

  size_t signatureLength = 0;
  const uint32_t started = millis();
  const int result = MLDSA44::sign(
      g_signature,
      &signatureLength,
      g_plaintext,
      messageLength,
      g_dsaSecretKey);

  if (result != 0 || signatureLength != MLDSA44::SIGNATURE_SIZE) {
    sendError("DSA_SIGN_FAILED");
    return;
  }

  Serial.printf("OK elapsed_ms=%lu\n", millis() - started);
  Serial.flush();
  demo_protocol::writeFrame(g_signature, signatureLength);
}

void handleSelfTest() {
  bool kemOk = false;
  bool aesOk = false;
  bool dsaOk = false;

  if (PQCLEAN_MLKEM768_CLEAN_crypto_kem_enc(
          g_kemCiphertext, g_sharedSecretCheck, g_kemPublicKey) == 0 &&
      PQCLEAN_MLKEM768_CLEAN_crypto_kem_dec(
          g_sharedSecret, g_kemCiphertext, g_kemSecretKey) == 0) {
    kemOk = memcmp(g_sharedSecret, g_sharedSecretCheck, SHARED_SECRET_SIZE) == 0;
  }

  const uint8_t selfTestMessage[] = "ESP32-S3-CAM PQC self-test";
  const uint8_t selfTestAad[] = "esp32-only/selftest/v1";
  const size_t selfTestLength = sizeof(selfTestMessage) - 1;
  const size_t selfTestAadLength = sizeof(selfTestAad) - 1;
  nextNonce(g_nonce);

  if (kemOk &&
      aesEncrypt(
          g_sharedSecret,
          g_nonce,
          selfTestAad,
          selfTestAadLength,
          selfTestMessage,
          selfTestLength,
          g_ciphertext,
          g_tag) == 0 &&
      aesDecrypt(
          g_sharedSecret,
          g_nonce,
          selfTestAad,
          selfTestAadLength,
          g_ciphertext,
          selfTestLength,
          g_tag,
          g_plaintext) == 0) {
    aesOk = memcmp(g_plaintext, selfTestMessage, selfTestLength) == 0;
  }

  size_t signatureLength = 0;
  if (MLDSA44::sign(
          g_signature,
          &signatureLength,
          selfTestMessage,
          selfTestLength,
          g_dsaSecretKey) == 0 &&
      signatureLength == MLDSA44::SIGNATURE_SIZE &&
      MLDSA44::verify(
          g_signature,
          signatureLength,
          selfTestMessage,
          selfTestLength,
          g_dsaPublicKey) == 0) {
    dsaOk = true;
  }

  Serial.printf(
      "SELFTEST MLKEM=%s AES=%s MLDSA=%s\n",
      kemOk ? "PASS" : "FAIL",
      aesOk ? "PASS" : "FAIL",
      dsaOk ? "PASS" : "FAIL");
  Serial.flush();
}

void resetSession() {
  clearSessionSecret();
  demo_protocol::writeLine("OK");
}

void handleCommand(const char *command) {
  if (strcmp(command, "INFO") == 0) {
    handleInfo();
  } else if (strcmp(command, "MEMORY_INFO") == 0) {
    handleMemoryInfo();
  } else if (strncmp(command, "SET_REKEY_INTERVAL ", 19) == 0) {
    handleSetRekeyInterval(command);
  } else if (strcmp(command, "SELFTEST") == 0) {
    handleSelfTest();
  } else if (strcmp(command, "GET_KEM_PUBLIC_KEY") == 0) {
    if (!g_kemKeyReady) {
      sendError("KEM_KEY_UNAVAILABLE");
    } else {
      demo_protocol::writeLine("OK");
      demo_protocol::writeFrame(g_kemPublicKey, sizeof(g_kemPublicKey));
    }
  } else if (strcmp(command, "KEM_DECAPSULATE") == 0) {
    handleKemDecapsulate();
  } else if (strcmp(command, "AES_DECRYPT") == 0) {
    handleAesDecrypt();
  } else if (strcmp(command, "AES_ENCRYPT") == 0) {
    handleAesEncrypt();
  } else if (strcmp(command, "CAMERA_INFO") == 0) {
    handleCameraInfo();
  } else if (strcmp(command, "CAMERA_MODE PHOTO") == 0) {
    handleCameraMode(true);
  } else if (strcmp(command, "CAMERA_MODE STREAM") == 0) {
    handleCameraMode(false);
  } else if (strcmp(command, "CAMERA_CAPTURE_ENCRYPTED") == 0) {
    handleCameraCaptureEncrypted();
  } else if (strcmp(command, "GET_DSA_PUBLIC_KEY") == 0) {
    demo_protocol::writeLine("OK");
    demo_protocol::writeFrame(g_dsaPublicKey, sizeof(g_dsaPublicKey));
  } else if (strcmp(command, "DSA_SIGN") == 0) {
    handleDsaSign();
  } else if (strcmp(command, "RESET_SESSION") == 0) {
    resetSession();
  } else {
    sendError("UNKNOWN_COMMAND");
  }
}

}  // namespace

bool initializeCrypto() {
  esp_fill_random(g_noncePrefix, sizeof(g_noncePrefix));

  Serial.println("Generating ML-KEM-768 keypair...");
  uint32_t started = millis();
  if (!generateKemKeypair()) {
    Serial.println("ERR ML-KEM key generation failed");
    return false;
  }
  Serial.printf("ML-KEM-768 keypair ready in %lu ms\n", millis() - started);

  Serial.println("Generating ML-DSA-44 keypair...");
  started = millis();
  if (MLDSA44::generateKeypair(g_dsaPublicKey, g_dsaSecretKey) != 0) {
    Serial.println("ERR ML-DSA key generation failed");
    return false;
  }
  Serial.printf("ML-DSA-44 keypair ready in %lu ms\n", millis() - started);
  Serial.printf("Free heap: %u bytes, PSRAM: %u bytes\n", ESP.getFreeHeap(), ESP.getPsramSize());
  return true;
}

void runProtocolLoop() {
  demo_protocol::writeLine("PQC-DEMO READY");

  char command[64];
  while (true) {
    if (demo_protocol::readLine(command, sizeof(command), 1000)) {
      handleCommand(command);
    }
  }
}
