#pragma once
// Host stub of the display driver: the 2D games only push their canvas to
// it, and name its colour constants.
#include <Adafruit_GFX.h>
#define ST7735_BLACK   0x0000
#define ST7735_WHITE   0xFFFF
#define ST7735_RED     0xF800
#define ST7735_GREEN   0x07E0
#define ST7735_BLUE    0x001F
#define ST7735_CYAN    0x07FF
#define ST7735_MAGENTA 0xF81F
#define ST7735_YELLOW  0xFFE0
#define ST7735_ORANGE  0xFC00
struct Adafruit_ST7735 {
    void drawRGBBitmap(int16_t, int16_t, const uint16_t*, int16_t, int16_t) {}
    uint8_t getRotation() const { return 1; }
};
