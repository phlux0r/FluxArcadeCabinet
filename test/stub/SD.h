#pragma once
// Host stub: no SD card, so every open fails and games take their fallbacks.
#include <cstddef>
#include <cstdint>
#define FILE_READ "r"
enum { CARD_NONE = 0 };
struct File { explicit operator bool() const { return false; } void close() {} size_t read(uint8_t*, size_t) { return 0; } bool seek(uint32_t) { return false; } size_t position() { return 0; } size_t size() { return 0; } int available() { return 0; } int read() { return -1; } };
struct SDT { bool begin(int) { return false; } File open(const char*, const char* = FILE_READ) { return File(); } int cardType() { return 0; } bool exists(const char*) { return false; } };
inline SDT SD;
