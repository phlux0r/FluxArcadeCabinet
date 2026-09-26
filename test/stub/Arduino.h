#pragma once
// Host stub of the Arduino core surface the cabinet uses. Mirrors
// arduino-esp32 where the difference would affect compilation (std::min/max
// rather than macros, constrain() as a macro), so a type error shows up here
// the same way it would in a real build.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <algorithm>

using std::min;
using std::max;
typedef uint8_t byte;

#define PROGMEM
#define PI 3.1415926535897932384626433832795
#define HIGH 1
#define LOW 0
#define INPUT 0
#define INPUT_PULLUP 2
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

inline uint16_t pgm_read_word(const void* p) { uint16_t v; memcpy(&v, p, 2); return v; }
inline uint8_t  pgm_read_byte(const void* p) { return *(const uint8_t*)p; }

// Fake clock: the harness advances this once per simulated frame, so runs are
// deterministic and finish far faster than real time.
extern unsigned long g_fakeMillis;
inline unsigned long millis() { return g_fakeMillis; }
inline unsigned long micros() { return g_fakeMillis * 1000UL; }
inline void delay(unsigned long) {}

// Defined by the harness as a seeded LCG, so a run is reproducible.
long random(long hi);
long random(long lo, long hi);
void randomSeed(unsigned long s);

inline float radians(float d) { return d * (float)PI / 180.0f; }
inline float degrees(float r) { return r * 180.0f / (float)PI; }
inline int  analogRead(int) { return 2048; }
inline int  digitalRead(int) { return HIGH; }
inline void pinMode(int, int) {}
