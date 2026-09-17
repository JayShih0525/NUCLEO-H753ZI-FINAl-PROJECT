#include "rekey_pipeline.h"
#include <Arduino.h>
#include <MLDSA44.h>
#include <atomic>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/md.h>
extern "C" {
#include "src/mlkem768/api.h"
}

namespace pending_rekey {
namespace {
constexpr char AUTH[] = "esp32-only/pipeline-auth/v1";
constexpr char CONFIRM[] = "esp32-only/pipeline-confirm/v1";
constexpr char COMMIT[] = "esp32-only/pipeline-commit/v1";
struct Workspace {
  uint8_t context[72], pk[1184], sk[2400], ct[1088], secret[32];
  uint8_t signature[2420], proof[32];
  uint8_t message[sizeof(CONFIRM) + 72 + 1184 + 1088];
  uint32_t keygenMs, signMs, decapMs, stackMin;
};
Workspace *work = nullptr;  // PSRAM, allocated only when pipeline is requested
TaskHandle_t task = nullptr;
std::atomic<State> phase{EMPTY};
const uint8_t *identity = nullptr;  // immutable persistent identity, not copied
bool decapsulate = false;
void wipe(void *p, size_t n) {
  volatile uint8_t *b = static_cast<volatile uint8_t *>(p);
  while (n--) *b++ = 0;
}
void u32(uint8_t *p, uint32_t n) {
  for (int i = 3; i >= 0; --i) { p[i] = n; n >>= 8; }
}
bool mac(const uint8_t *key, const uint8_t *data, size_t n, uint8_t *out) {
  return mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                         key, 32, data, n, out) == 0;
}
void worker(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    // Acquire the job and its immutable inputs; publish outputs only at completion.
    if (phase.load(std::memory_order_acquire) != WORKING) continue;
    bool ok;
    uint32_t started = millis();
    if (!decapsulate) {
      ok = PQCLEAN_MLKEM768_CLEAN_crypto_kem_keypair(work->pk, work->sk) == 0;
      work->keygenMs = millis() - started;
      memcpy(work->message, AUTH, sizeof(AUTH));
      memcpy(work->message + sizeof(AUTH), work->context, 72);
      memcpy(work->message + sizeof(AUTH) + 72, work->pk, 1184);
      size_t length = 0;
      started = millis();
      ok = ok && MLDSA44::sign(work->signature, &length, work->message,
                              sizeof(AUTH) + 72 + 1184, identity) == 0 && length == 2420;
      work->signMs = millis() - started;
    } else {
      ok = PQCLEAN_MLKEM768_CLEAN_crypto_kem_dec(work->secret, work->ct, work->sk) == 0;
      memcpy(work->message, CONFIRM, sizeof(CONFIRM));
      memcpy(work->message + sizeof(CONFIRM), work->context, 72);
      memcpy(work->message + sizeof(CONFIRM) + 72, work->pk, 1184);
      memcpy(work->message + sizeof(CONFIRM) + 72 + 1184, work->ct, 1088);
      ok = ok && mac(work->secret, work->message, sizeof(CONFIRM) + 72 + 1184 + 1088, work->proof);
      work->decapMs = millis() - started;
    }
    work->stackMin = uxTaskGetStackHighWaterMark(nullptr);
    wipe(work->message, sizeof(work->message));
    phase.store(ok ? (decapsulate ? CONFIRMED : OFFER) : FAILED, std::memory_order_release);
  }
}
bool allocate() {
  if (task) return true;
  work = static_cast<Workspace *>(heap_caps_calloc(1, sizeof(Workspace), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!work) return false;
  // ESP-IDF task stack size is in bytes. Allocation failure is reported to Host.
  if (xTaskCreate(worker, "pqc-next", 49152, nullptr, 1, &task) != pdPASS) {
    heap_caps_free(work); work = nullptr; task = nullptr; return false;
  }
  return true;
}
}
State state() { return phase.load(std::memory_order_acquire); }
void discard() {
  // Worker has no network waits; let crypto finish before destroying its inputs.
  while (state() == WORKING) vTaskDelay(1);
  if (work) wipe(work, sizeof(*work));
  identity = nullptr;
  phase.store(EMPTY, std::memory_order_release);
}
bool start(const uint8_t context[72], const uint8_t *identitySecret) {
  if (state() != EMPTY || !allocate()) return false;
  memcpy(work->context, context, 72);
  identity = identitySecret;
  decapsulate = false;
  phase.store(WORKING, std::memory_order_release);
  xTaskNotifyGive(task);
  return true;
}
bool install(const uint8_t ciphertext[1088]) {
  if (state() != OFFER) return false;
  memcpy(work->ct, ciphertext, 1088);
  decapsulate = true;
  phase.store(WORKING, std::memory_order_release);
  xTaskNotifyGive(task);
  return true;
}
size_t reply(uint8_t *out) {
  const State s = state();
  out[0] = s == EMPTY ? 0 : s == WORKING ? 1 : s == OFFER ? 2 : s == CONFIRMED ? 3 : 4;
  if (s == OFFER) {
    memcpy(out + 1, work->context, 72);
    memcpy(out + 73, work->pk, 1184);
    memcpy(out + 1257, work->signature, 2420);
    u32(out + 3677, work->keygenMs); u32(out + 3681, work->signMs);
    return 3685;
  }
  if (s == CONFIRMED) {
    memcpy(out + 1, work->proof, 32);
    u32(out + 33, work->decapMs); u32(out + 37, work->stackMin);
    return 41;
  }
  return 1;
}
bool promote(const uint8_t token[32], uint8_t *pk, uint8_t *sk, uint8_t *ct, uint8_t *secret) {
  if (state() != CONFIRMED) return false;
  uint8_t message[sizeof(COMMIT) + 72], expected[32];
  memcpy(message, COMMIT, sizeof(COMMIT)); memcpy(message + sizeof(COMMIT), work->context, 72);
  if (!mac(work->secret, message, sizeof(message), expected)) return false;
  uint8_t different = 0;
  for (size_t i = 0; i < 32; ++i) different |= expected[i] ^ token[i];
  wipe(expected, sizeof(expected));
  if (different) return false;
  memcpy(pk, work->pk, 1184); memcpy(sk, work->sk, 2400);
  memcpy(ct, work->ct, 1088); memcpy(secret, work->secret, 32);
  discard();
  return true;
}
}
