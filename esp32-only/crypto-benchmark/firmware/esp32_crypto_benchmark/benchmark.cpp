// Standalone primitives only: no camera, Wi-Fi, AES, handshake or NVS access.
#include <Arduino.h>
#include <esp_arduino_version.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <bootloader_random.h>
#include "benchmark_build.h"
#include "src/dsa/MLDSA44.h"
extern "C" {
#include "src/kem/api.h"
}

static_assert(MLDSA44::PUBLIC_KEY_SIZE == 1312 && MLDSA44::SECRET_KEY_SIZE == 2560 && MLDSA44::SIGNATURE_SIZE == 2420, "ML-DSA-44 sizes changed");
static_assert(PQCLEAN_MLKEM768_CLEAN_CRYPTO_PUBLICKEYBYTES == 1184 && PQCLEAN_MLKEM768_CLEAN_CRYPTO_SECRETKEYBYTES == 2400 && PQCLEAN_MLKEM768_CLEAN_CRYPTO_CIPHERTEXTBYTES == 1088 && PQCLEAN_MLKEM768_CLEAN_CRYPTO_BYTES == 32, "ML-KEM-768 sizes changed");

static_assert(CONFIG_IDF_TARGET_ESP32S3, "This benchmark configuration targets ESP32-S3");
constexpr uint32_t BAUD = 921600;
constexpr uint32_t STACK_BYTES = 96 * 1024; // IDF task stack/watermark units are bytes.
constexpr size_t MAX_MESSAGE = 4096;
enum Operation { NOOP, KEM_KEYGEN, KEM_ENCAPS, KEM_DECAPS, DSA_KEYGEN, DSA_SIGN, DSA_VERIFY, OP_COUNT };
enum Job { PREPARE, MEASURE, CHECK };
const char *const OP_NAMES[] = {"baseline", "mlkem768_keygen", "mlkem768_encaps", "mlkem768_decaps", "mldsa44_keygen", "mldsa44_sign", "mldsa44_verify"};
struct KemBuffers { uint8_t pk[1184], sk[2400], ct[1088], ss[32], recovered[32]; };
struct DsaBuffers { uint8_t pk[1312], sk[2560], sig[2420]; size_t siglen; };
union CryptoBuffers { KemBuffers kem; DsaBuffers dsa; };
static CryptoBuffers buffers;
static uint8_t message[MAX_MESSAGE];
static StackType_t workerStack[STACK_BYTES / sizeof(StackType_t)];
static StaticTask_t workerTcb;
static TaskHandle_t controller;
static Operation currentOp;
static Job currentJob;
static bool memoryPass;
static size_t messageLength;
static int jobResult;
struct Sample {
  int rc, monitorStart, monitorStop;
  int64_t elapsedUs;
  size_t stackUsed, stackFree;
  size_t internalBefore, internalAfter, internalLow, internalLargest;
  size_t psramBefore, psramAfter, psramLow;
};
static Sample sample;
constexpr uint32_t INTERNAL_CAPS = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
constexpr uint32_t PSRAM_CAPS = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

int invokePrimitive() {
  switch (currentOp) {
    case NOOP: asm volatile("" ::: "memory"); return 0;
    case KEM_KEYGEN: return PQCLEAN_MLKEM768_CLEAN_crypto_kem_keypair(buffers.kem.pk, buffers.kem.sk);
    case KEM_ENCAPS: return PQCLEAN_MLKEM768_CLEAN_crypto_kem_enc(buffers.kem.ct, buffers.kem.ss, buffers.kem.pk);
    case KEM_DECAPS: return PQCLEAN_MLKEM768_CLEAN_crypto_kem_dec(buffers.kem.recovered, buffers.kem.ct, buffers.kem.sk);
    case DSA_KEYGEN: return MLDSA44::generateKeypair(buffers.dsa.pk, buffers.dsa.sk);
    case DSA_SIGN: return MLDSA44::sign(buffers.dsa.sig, &buffers.dsa.siglen, message, messageLength, buffers.dsa.sk);
    case DSA_VERIFY: return MLDSA44::verify(buffers.dsa.sig, buffers.dsa.siglen, message, messageLength, buffers.dsa.pk);
    default: return -1;
  }
}

int prepareInputs() {
  int rc = 0;
  memset(&buffers, 0, sizeof(buffers));
  if (currentOp == KEM_ENCAPS || currentOp == KEM_DECAPS) {
    rc = PQCLEAN_MLKEM768_CLEAN_crypto_kem_keypair(buffers.kem.pk, buffers.kem.sk);
    if (!rc && currentOp == KEM_DECAPS)
      rc = PQCLEAN_MLKEM768_CLEAN_crypto_kem_enc(buffers.kem.ct, buffers.kem.ss, buffers.kem.pk);
  } else if (currentOp == DSA_SIGN || currentOp == DSA_VERIFY) {
    rc = MLDSA44::generateKeypair(buffers.dsa.pk, buffers.dsa.sk);
    if (!rc && currentOp == DSA_VERIFY)
      rc = MLDSA44::sign(buffers.dsa.sig, &buffers.dsa.siglen, message, messageLength, buffers.dsa.sk);
  }
  return rc;
}

int validateOutput() {
  int rc = 0;
  if (currentOp == NOOP) return 0;
  if (currentOp <= KEM_DECAPS) {
    if (currentOp == KEM_KEYGEN)
      rc = PQCLEAN_MLKEM768_CLEAN_crypto_kem_enc(buffers.kem.ct, buffers.kem.ss, buffers.kem.pk);
    if (!rc && currentOp != KEM_DECAPS)
      rc = PQCLEAN_MLKEM768_CLEAN_crypto_kem_dec(buffers.kem.recovered, buffers.kem.ct, buffers.kem.sk);
    if (rc || memcmp(buffers.kem.ss, buffers.kem.recovered, 32)) return -100;
    // ML-KEM implicit rejection need not return an error; check changed secret.
    buffers.kem.ct[0] ^= 1;
    rc = PQCLEAN_MLKEM768_CLEAN_crypto_kem_dec(buffers.kem.recovered, buffers.kem.ct, buffers.kem.sk);
    return (rc == 0 && memcmp(buffers.kem.ss, buffers.kem.recovered, 32) != 0) ? 0 : -101;
  }
  if (currentOp == DSA_KEYGEN)
    rc = MLDSA44::sign(buffers.dsa.sig, &buffers.dsa.siglen, message, messageLength, buffers.dsa.sk);
  if (rc || buffers.dsa.siglen != MLDSA44::SIGNATURE_SIZE) return -102;
  rc = MLDSA44::verify(buffers.dsa.sig, buffers.dsa.siglen, message, messageLength, buffers.dsa.pk);
  if (rc) return -103;
  buffers.dsa.sig[0] ^= 1;
  return MLDSA44::verify(buffers.dsa.sig, buffers.dsa.siglen, message, messageLength, buffers.dsa.pk) != 0 ? 0 : -104;
}

void worker(void *) {
  if (currentJob == PREPARE) jobResult = prepareInputs();
  else if (currentJob == CHECK) jobResult = validateOutput();
  else {
    sample.monitorStart = sample.monitorStop = -1;
    if (memoryPass) {
      sample.monitorStart = heap_caps_monitor_local_minimum_free_size_start();
      sample.internalBefore = heap_caps_get_free_size(INTERNAL_CAPS);
      sample.psramBefore = heap_caps_get_free_size(PSRAM_CAPS);
    }
    const int64_t start = esp_timer_get_time();
    sample.rc = invokePrimitive();
    sample.elapsedUs = esp_timer_get_time() - start;
    // Capture before any validation or serial output; new task for EVERY sample.
    sample.stackFree = uxTaskGetStackHighWaterMark(nullptr);
    sample.stackUsed = STACK_BYTES - sample.stackFree;
    if (memoryPass) {
      sample.internalAfter = heap_caps_get_free_size(INTERNAL_CAPS);
      sample.psramAfter = heap_caps_get_free_size(PSRAM_CAPS);
      sample.internalLow = heap_caps_get_minimum_free_size(INTERNAL_CAPS);
      sample.psramLow = heap_caps_get_minimum_free_size(PSRAM_CAPS);
      sample.internalLargest = heap_caps_get_largest_free_block(INTERNAL_CAPS);
      if (sample.monitorStart == ESP_OK)
        sample.monitorStop = heap_caps_monitor_local_minimum_free_size_stop();
    }
    jobResult = sample.rc;
  }
  xTaskNotifyGive(controller);
  vTaskSuspend(nullptr); // Controller deletes this task; no deferred self-delete reuse.
}

bool runJob(Job job) {
  currentJob = job;
  jobResult = -999;
  TaskHandle_t task = xTaskCreateStaticPinnedToCore(worker, "crypto_bench", STACK_BYTES,
      nullptr, 1, workerStack, &workerTcb, xPortGetCoreID());
  if (!task) return false;
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  vTaskDelete(task);
  return jobResult == 0;
}

size_t primitiveIoBytes() {
  switch (currentOp) {
    case KEM_KEYGEN: return 1184 + 2400;
    case KEM_ENCAPS: return 1184 + 1088 + 32;
    case KEM_DECAPS: return 2400 + 1088 + 32;
    case DSA_KEYGEN: return 1312 + 2560;
    case DSA_SIGN: return 2560 + 2420 + messageLength;
    case DSA_VERIFY: return 1312 + 2420 + messageLength;
    default: return 0;
  }
}

void printMetadata(const char *type, const char *probe = "") {
  Serial.printf("{\"type\":\"%s\",\"probe_id\":\"%s\",\"schema\":1,\"firmware\":\"esp32-crypto-benchmark\",\"build_id\":\"%s\",\"mlkem_sha256\":\"%s\",\"mldsa_sha256\":\"%s\",\"mldsa_version\":\"%s\",\"arduino_core\":\"%s\",\"idf\":\"%s\",\"chip\":\"ESP32-S3\",\"chip_revision\":%u,\"cpu_mhz\":%u,\"flash_bytes\":%u,\"psram_bytes\":%u,\"baud\":%u,\"worker_core\":%d,\"worker_stack_reserved_bytes\":%u,\"crypto_union_reserved_bytes\":%u,\"message_reserved_bytes\":%u,\"rng\":\"SAR entropy enabled; RF off\",\"heap_method\":\"IDF local minimum per-region aggregate; system-wide\"}\n",
      type, probe, BENCH_BUILD_ID, BENCH_KEM_HASH, BENCH_DSA_HASH, BENCH_DSA_VERSION,
      ESP_ARDUINO_VERSION_STR, esp_get_idf_version(), ESP.getChipRevision(), getCpuFrequencyMhz(),
      ESP.getFlashChipSize(), ESP.getPsramSize(), BAUD, xPortGetCoreID(), STACK_BYTES,
      (unsigned)sizeof(buffers), (unsigned)sizeof(message));
}

void executeRun(const char *id, unsigned rounds, unsigned warmups, unsigned length) {
  messageLength = length;
  for (size_t i = 0; i < MAX_MESSAGE; ++i) message[i] = (i * 29 + 7) & 255;
  printMetadata("metadata");
  Serial.printf("{\"type\":\"begin\",\"run_id\":\"%s\",\"rounds\":%u,\"warmups\":%u,\"message_bytes\":%u,\"expected_samples\":%u}\n", id, rounds, warmups, length, rounds * OP_COUNT * 2);
  Serial.flush();
  unsigned emitted = 0;
  for (unsigned phase = 0; phase < 2; ++phase) {
    memoryPass = phase == 1;
    for (unsigned op = 0; op < OP_COUNT; ++op) {
      currentOp = static_cast<Operation>(op);
      for (unsigned i = 0; i < rounds + warmups; ++i) {
        sample = {};
        bool ok = runJob(PREPARE);
        const char *stage = "prepare";
        if (ok) { stage = "measure"; ok = runJob(MEASURE); }
        if (ok) { stage = "validate"; ok = runJob(CHECK); }
        if (!ok || (memoryPass && (sample.monitorStart || sample.monitorStop))) {
          Serial.printf("{\"type\":\"error\",\"run_id\":\"%s\",\"operation\":\"%s\",\"stage\":\"%s\",\"rc\":%d,\"monitor_start\":%d,\"monitor_stop\":%d}\n", id, OP_NAMES[op], stage, jobResult, sample.monitorStart, sample.monitorStop);
          Serial.printf("{\"type\":\"end\",\"run_id\":\"%s\",\"ok\":false,\"samples\":%u}\n", id, emitted);
          memset(&buffers, 0, sizeof(buffers));
          return;
        }
        if (i >= warmups) {
          Serial.printf("{\"type\":\"sample\",\"run_id\":\"%s\",\"phase\":\"%s\",\"operation\":\"%s\",\"iteration\":%u,\"elapsed_us\":%lld,\"rc\":%d,\"validation_ok\":true,\"stack_used_bytes\":%u,\"stack_free_bytes\":%u,\"primitive_io_bytes\":%u",
            id, phase ? "memory" : "timing", OP_NAMES[op], i - warmups + 1,
            (long long)sample.elapsedUs, sample.rc, (unsigned)sample.stackUsed,
            (unsigned)sample.stackFree, (unsigned)primitiveIoBytes());
          if (memoryPass) {
            Serial.printf(",\"internal_free_before_bytes\":%u,\"internal_free_after_bytes\":%u,\"internal_low_bytes\":%u,\"internal_largest_after_bytes\":%u,\"psram_free_before_bytes\":%u,\"psram_free_after_bytes\":%u,\"psram_low_bytes\":%u,\"monitor_start\":%d,\"monitor_stop\":%d",
              (unsigned)sample.internalBefore, (unsigned)sample.internalAfter,
              (unsigned)sample.internalLow, (unsigned)sample.internalLargest,
              (unsigned)sample.psramBefore, (unsigned)sample.psramAfter,
              (unsigned)sample.psramLow, sample.monitorStart, sample.monitorStop);
          }
          Serial.println("}");
          ++emitted;
        }
        delay(1); // Let idle task/watchdog run; not inside measurement.
      }
    }
  }
  // Volatile erase so secret-bearing test buffers do not remain after the run.
  volatile uint8_t *p = reinterpret_cast<volatile uint8_t *>(&buffers);
  for (size_t i = 0; i < sizeof(buffers); ++i) p[i] = 0;
  Serial.printf("{\"type\":\"end\",\"run_id\":\"%s\",\"ok\":true,\"samples\":%u}\n", id, emitted);
}

void setup() {
  Serial.begin(BAUD);
  delay(800);
  controller = xTaskGetCurrentTaskHandle();
  // No Wi-Fi/Bluetooth/ADC is used; enable the SAR ADC entropy source explicitly.
  bootloader_random_enable();
  printMetadata("ready");
}

void loop() {
  static char line[128];
  static size_t used = 0;
  static bool overflow = false;
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (used < sizeof(line) - 1) line[used++] = c;
      else overflow = true;
      continue;
    }
    line[used] = 0;
    if (!overflow && !strcmp(line, "HELLO")) printMetadata("ready");
    else if (!overflow) {
      char id[33], trailing;
      unsigned rounds, warmups, length;
      if (sscanf(line, "HELLO %32[0-9a-f] %c", id, &trailing) == 1)
        printMetadata("ready", id);
      else if (sscanf(line, "RUN %32[0-9a-f] %u %u %u %c", id, &rounds, &warmups, &length, &trailing) == 4 &&
          rounds >= 1 && rounds <= 1000 && warmups <= 20 && length <= MAX_MESSAGE)
        executeRun(id, rounds, warmups, length);
      else Serial.println("{\"type\":\"command_error\",\"message\":\"Use HELLO or RUN hex_id rounds(1..1000) warmups(0..20) message_bytes(0..4096)\"}");
    } else Serial.println("{\"type\":\"command_error\",\"message\":\"Line too long\"}");
    used = 0;
    overflow = false;
  }
  delay(5);
}
