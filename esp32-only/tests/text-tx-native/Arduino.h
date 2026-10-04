#pragma once
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
using std::min;
inline uint32_t nowMs=0;
inline uint32_t millis(){return nowMs;}
inline void delay(uint32_t ms){nowMs+=ms;}
class Stream { public:
 std::string input;
 int available(){return input.size();}
 int read(){if(input.empty()) return -1; const auto value=uint8_t(input[0]); input.erase(0,1); return value;}
 size_t readBytes(uint8_t* out,size_t n){n=std::min(n,input.size());memcpy(out,input.data(),n);input.erase(0,n);return n;}
 void println(const char*){}
};
struct SerialStub:Stream {
 template<class... A> void printf(const char*,A...){}
};
inline SerialStub Serial;
