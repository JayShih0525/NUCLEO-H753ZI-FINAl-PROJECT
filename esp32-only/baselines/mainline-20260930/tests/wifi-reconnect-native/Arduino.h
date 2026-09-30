#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
extern uint32_t clockMs;
inline uint32_t millis(){return clockMs;}
inline void delay(uint32_t n){clockMs+=n;}
class Stream {public: size_t write(const uint8_t*,size_t n){return n;} };
struct SerialStub:Stream {template<class... T> void printf(const char*,T...){} void println(const char*){} void flush(){} };
inline SerialStub Serial;
