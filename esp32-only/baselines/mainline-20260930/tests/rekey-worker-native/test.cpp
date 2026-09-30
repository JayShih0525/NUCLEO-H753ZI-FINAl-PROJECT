#include <cassert>
#include <future>
#include <cstdio>
#include "../../firmware/esp32_pqc_demo/rekey_pipeline.cpp"
#include "../../firmware/esp32_pqc_demo/pipeline_rules.h"
extern "C" int PQCLEAN_MLKEM768_CLEAN_crypto_kem_keypair(uint8_t *pk, uint8_t *sk) {
  memset(pk, 1, 1184); memset(sk, 2, 2400); return 0;
}
extern "C" int PQCLEAN_MLKEM768_CLEAN_crypto_kem_dec(uint8_t *key, const uint8_t *, const uint8_t *) {
  memset(key, 3, 32); return 0;
}
void waitFor(pending_rekey::State expected) {
  for (int i = 0; i < 2000 && pending_rekey::state() != expected; ++i) vTaskDelay(1);
  assert(pending_rekey::state() == expected);
}
int main() {
  using namespace pending_rekey;
  assert(validRequest(1, 1, 32, 1, 0, 1, 0, 10, false));
  assert(!validRequest(2, 1, 0, 1, 0, 1, 0, 10, false));
  assert(!validRequest(1, 1, 31, 1, 0, 1, 0, 10, false));
  assert(!validRequest(1, 1, 32, 2, 0, 1, 0, 10, false));
  assert(!validRequest(2, 1, 0, 1, 1, 1, 2, 10, true));
  assert(!validRequest(4, 1, 32, 1, 9, 1, 9, 10, true));
  assert(validRequest(4, 1, 32, 1, 10, 1, 10, 10, true));
  assert(validRequest(2, 0, 0, 1, 10, 1, 10, 10, true));
  assert(validRequest(3, 0, 1088, 1, 10, 1, 10, 10, true));
  for (unsigned action = 0; action < 4; ++action) {
    const size_t lengths[] = {0, 32, 0, 1088};
    assert(!validRequest(action, 1, lengths[action], 1, 10, 1, 10, 10, true));
  }
  assert(!validRequest(4, 1, 32, UINT32_MAX, 10, UINT32_MAX, 10, 10, true));
  assert(!validRequest(255, 1, 0, 1, 1, 1, 1, 10, true));
  uint8_t context[72]{}, identity[2560]{}, ct[1088]{}, out[3685];
  uint8_t pk[1184]{}, sk[2400]{}, key[32]{}, token[32]{};
  failAllocation = true; assert(!start(context, identity)); failAllocation = false;
  failTask = true; assert(!start(context, identity)); failTask = false;
  holdCrypto = true;
  assert(start(context, identity)); assert(!start(context, identity));
  assert(state() == WORKING && reply(out) == 1 && out[0] == 1);
  assert(!install(ct)); assert(!promote(token, pk, sk, ct, key));
  auto cleanup = std::async(std::launch::async, [] { discard(); });
  assert(cleanup.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
  holdCrypto = false; cleanup.get();
  assert(state() == EMPTY);
  for (size_t i = 0; i < sizeof(*work); ++i) assert(reinterpret_cast<uint8_t *>(work)[i] == 0);
  assert(start(context, identity)); waitFor(OFFER);
  assert(reply(out) == 3685 && out[0] == 2);
  assert(install(ct)); waitFor(CONFIRMED);
  assert(reply(out) == 41 && out[0] == 3);
  assert(!promote(token, pk, sk, ct, key));  // Wrong token cannot activate.
  uint8_t message[sizeof(COMMIT) + 72];
  memcpy(message, COMMIT, sizeof(COMMIT)); memcpy(message + sizeof(COMMIT), context, 72);
  assert(mac(work->secret, message, sizeof(message), token));
  assert(promote(token, pk, sk, ct, key));
  assert(state() == EMPTY && pk[0] == 1 && sk[0] == 2 && key[0] == 3);
  assert(!promote(token, pk, sk, ct, key));  // A commit cannot be repeated.
  failSign = true; assert(start(context, identity)); waitFor(FAILED); discard();
  assert(state() == EMPTY);
  puts("PASS: worker allocation, concurrent discard/join, promotion, replay and failure cleanup");
  fflush(stdout);
  std::_Exit(0);  // Test worker intentionally lives until process exit, as on ESP32.
}
