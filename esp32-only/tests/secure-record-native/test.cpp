#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include "../../firmware/esp32_pqc_demo/secure_records.cpp"

std::vector<uint8_t> unhex(const char *s) {
  std::vector<uint8_t> bytes;
  while (*s) { unsigned value; assert(sscanf(s, "%2x", &value) == 1); bytes.push_back(value); s += 2; }
  return bytes;
}
int main(int argc, char **argv) {
  assert(argc == 4);
  const auto hash = unhex(argv[1]), incoming = unhex(argv[2]), expected = unhex(argv[3]);
  uint8_t secret[32]; for (int i = 0; i < 32; ++i) secret[i] = i;
  assert(hash.size() == 32 && secure_records::start(secret, hash.data(), 1));
  size_t length = 0;
  assert(secure_records::inspect(incoming.data(), length));
  assert(secure_records::verify(incoming.data(), length));
  assert(!secure_records::verify(incoming.data(), length)); // replay
  uint8_t output[4160];
  const uint8_t response[] = {'O','K','\n'};
  assert(secure_records::encode(output, response, 3));
  assert(expected.size() == 67 && !memcmp(output, expected.data(), 67));
  // Each changed byte must fail, with a fresh expected sequence each time.
  for (size_t i = 0; i < incoming.size(); ++i) {
    assert(secure_records::start(secret, hash.data(), 1));
    auto bad = incoming; bad[i] ^= 1;
    assert(!secure_records::verify(bad.data(), incoming.size()-64));
  }
  secure_records::reset();
  assert(!secure_records::active() && !secure_records::encode(output, response, 3));
  puts("Native record golden vectors, replay, all-byte tampering: PASS");
}
