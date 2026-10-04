#include <cassert>
#include <string>
#include <vector>
#include "../../firmware/esp32_pqc_demo/protocol.cpp"
#include "../../firmware/esp32_pqc_demo/secure_records.cpp"
namespace demo_transport {
std::string output;
bool live = true;
Stream stream;
bool wifiMode() { return true; }
bool connected() { return live; }
void closeConnection() { live = false; }
Stream &io() { return stream; }
void flush() {}
size_t write(const uint8_t *p, size_t n) { output.append(reinterpret_cast<const char*>(p), n); return n; }
}
std::vector<uint8_t> unhex(const char *s) {
  std::vector<uint8_t> bytes;
  while (*s) { unsigned value; assert(sscanf(s, "%2x", &value) == 1); bytes.push_back(value); s += 2; }
  return bytes;
}
int main(int argc, char **argv) {
  assert(argc == 4);
  using namespace demo_transport;
  const auto hash = unhex(argv[1]), incoming = unhex(argv[2]), expected = unhex(argv[3]);
  uint8_t secret[32]; for (int i=0;i<32;++i) secret[i]=i;
  auto restart = [&]() {
    demo_protocol::resetRecords(); stream.input.clear(); output.clear(); live=true; nowMs=0;
    assert(demo_protocol::startRecords(secret, hash.data(), 1));
  };
  restart();
  stream.input.assign(reinterpret_cast<const char*>(incoming.data()), incoming.size());
  char line[64];
  assert(demo_protocol::readLine(line,sizeof(line),1000) && !strcmp(line,"INFO"));
  assert(demo_protocol::writeResponse("OK\n",nullptr,0));
  assert(output==std::string(reinterpret_cast<const char*>(expected.data()),expected.size()));
  // A second copy never reaches the command dispatcher.
  stream.input.assign(reinterpret_cast<const char*>(incoming.data()), incoming.size());
  assert(!demo_protocol::readLine(line,sizeof(line),1000) && !live);
  restart();
  stream.input.assign(reinterpret_cast<const char*>(incoming.data()), incoming.size()-1);
  assert(!demo_protocol::readLine(line,sizeof(line),1000) && !live && nowMs>=10000);
  restart();
  stream.input="RESET_SESSION\n"+std::string(40,' ');
  assert(!demo_protocol::readLine(line,sizeof(line),1000) && !live);
  restart();
  assert(!demo_protocol::readLine(line,sizeof(line),1000) && live && nowMs==1000);
  // New full handshake boundary may not discard unconsumed authenticated bytes.
  stream.input.assign(reinterpret_cast<const char*>(incoming.data()), incoming.size());
  assert(demo_protocol::inputAvailable()>0);
  assert(!demo_protocol::startRecords(secret, hash.data(), 2) && !live);
  puts("Native production protocol authenticated read/write, truncation, injection, idle: PASS");
}
