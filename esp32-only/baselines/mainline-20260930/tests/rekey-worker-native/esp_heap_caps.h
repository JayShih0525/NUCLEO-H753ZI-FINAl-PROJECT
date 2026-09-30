#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
inline bool failAllocation = false;
inline void *heap_caps_calloc(size_t n, size_t size, int) { return failAllocation ? nullptr : calloc(n, size); }
inline void heap_caps_free(void *p) { free(p); }
