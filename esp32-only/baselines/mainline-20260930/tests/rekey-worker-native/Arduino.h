#pragma once
#include <cstdint>
#include <cstring>
#include <chrono>
inline uint32_t millis() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
