#include <cassert>
#include <vector>
#include "../../firmware/esp32_pqc_demo/frame_writer.h"
int main() {
  for (size_t length : {size_t(0), size_t(12), size_t(16), size_t(20), size_t(32), size_t(33), size_t(5000)}) {
    std::vector<uint8_t> data(length, 0xa5), baseline, combined;
    int calls=0;
    assert(demo_protocol::writeFramedBytes(data.data(), length, false,
        [&](const uint8_t *p,size_t n){baseline.insert(baseline.end(),p,p+n);return true;}));
    assert(demo_protocol::writeFramedBytes(data.data(), length, true,
        [&](const uint8_t *p,size_t n){++calls;combined.insert(combined.end(),p,p+n);return true;}));
    assert(baseline==combined);
    assert(calls==(length<=32 ? 1 : 2));
  }
  int calls=0;
  uint8_t data[33]={};
  assert(!demo_protocol::writeFramedBytes(data,33,true,
      [&](const uint8_t*,size_t){++calls;return false;}));
  assert(calls==1); // No payload after failed header.
  assert(!demo_protocol::writeFramedBytes(nullptr,1,true,
      [](const uint8_t*,size_t){assert(false);return true;}));
}
