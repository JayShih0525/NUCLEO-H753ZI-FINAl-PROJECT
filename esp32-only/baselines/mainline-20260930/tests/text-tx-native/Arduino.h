#pragma once
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
using std::min;
inline uint32_t nowMs=0;
inline uint32_t millis(){return nowMs;}
inline void delay(uint32_t ms){nowMs+=ms;}
class Stream { public:
 int available(){return 0;} int read(){return -1;}
 size_t readBytes(uint8_t*,size_t){return 0;}
 void println(const char*){}
};
struct SerialStub:Stream {
 template<class... A> void printf(const char*,A...){}
};
inline SerialStub Serial;
