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
#define INITR_BLACKTAB 0
struct Adafruit_ST7735 {
    Adafruit_ST7735() {}
    Adafruit_ST7735(int, int, int) {}
    void initR(int) {}
    void setSPISpeed(unsigned long) {}
    void setRotation(uint8_t r) { _rot = r; }
    uint8_t getRotation() const { return _rot; }
    int16_t width() const  { return (_rot & 1) ? 160 : 128; }
    int16_t height() const { return (_rot & 1) ? 128 : 160; }
    void drawRGBBitmap(int16_t, int16_t, const uint16_t*, int16_t, int16_t) { ++frames; }
    void fillScreen(uint16_t) {}
    void fillRect(int16_t, int16_t, int16_t, int16_t, uint16_t) {}
    void setFont(const void* = nullptr) {}
    void setTextSize(uint8_t) {}
    void setTextColor(uint16_t) {}
    void setCursor(int16_t, int16_t) {}
    void print(const char*) {}
    long frames = 0;   // pushes to the display, for tests
private:
    uint8_t _rot = 0;
};
