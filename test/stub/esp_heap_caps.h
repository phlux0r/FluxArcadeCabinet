#pragma once
// Host stub: plain malloc for the audio engine's PSRAM allocations.
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
inline void* heap_caps_malloc(size_t n, int) { return malloc(n); }
inline void heap_caps_free(void* p) { free(p); }
