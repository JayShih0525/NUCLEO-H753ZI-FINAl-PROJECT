#include <cassert>
#include <initializer_list>
#include "../firmware/esp32_pqc_demo/mutual_policy.h"
int main() {
  using namespace mutual_policy;
  assert(publicCommand("INFO")); assert(publicCommand("GET_DSA_PUBLIC_KEY"));
  for (const char *c : {"CAMERA_PIPELINED", "CAMERA_CAPTURE_ENCRYPTED", "AES_ECHO", "DSA_SIGN", "RESET_SESSION", "MEMORY_INFO", "CAMERA_MODE STREAM"})
    assert(!publicCommand(c));
  for (const char *c : {"KEM_DECAPSULATE", "KEM_DECAPSULATE_INLINE", "AUTH_KEM", "AUTH_KEM_INLINE", "CONFIRM_SESSION", "CONFIRM_SESSION_INLINE", "GET_KEM_PUBLIC_KEY", "SET_REKEY_INTERVAL 10", "SELFTEST"})
    assert(legacyCommand(c));
  assert(!legacyCommand("CAMERA_PIPELINED"));
}
