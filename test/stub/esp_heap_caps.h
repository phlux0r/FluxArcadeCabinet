#pragma once
// Host stub: plain malloc for the audio engine's PSRAM allocations.
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
inline size_t heap_caps_get_free_size(int) { return 200000; }
inline void* heap_caps_malloc(size_t n, int) { return malloc(n); }
inline void heap_caps_free(void* p) { free(p); }
