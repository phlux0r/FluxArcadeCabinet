#pragma once
// Host stub of GFXcanvas16. Keeps a real RGB565 framebuffer so Jet renders
// into it exactly as on hardware and the harness can hash the result.
// The 2D drawing calls only need to be pixel-accurate where they'd change
// that hash: shapes the HUD draws over the 3D scene are no-ops here, and
// getTextBounds() returns the built-in font's fixed 6x8 cell so layout
// arithmetic matches.
#include <Arduino.h>

struct GFXfont;

class GFXcanvas16 {
public:
    GFXcanvas16(int16_t w, int16_t h) : _w(w), _h(h), _buf(new uint16_t[w * h]()) {}
    ~GFXcanvas16() { delete[] _buf; }

    uint16_t* getBuffer() { return _buf; }
    int16_t width() const  { return _w; }
    int16_t height() const { return _h; }

    void drawPixel(int16_t x, int16_t y, uint16_t c) {
        if (x >= 0 && y >= 0 && x < _w && y < _h) _buf[y * _w + x] = c;
    }
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
        for (int j = y; j < y + h; ++j)
            for (int i = x; i < x + w; ++i) drawPixel(i, j, c);
    }
    void fillScreen(uint16_t c) { fillRect(0, 0, _w, _h, c); }
    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) {
        for (int i = x; i < x + w; ++i) drawPixel(i, y, c);
    }
    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) {
        for (int j = y; j < y + h; ++j) drawPixel(x, j, c);
    }
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
        drawFastHLine(x, y, w, c);
        drawFastHLine(x, y + h - 1, w, c);
        drawFastVLine(x, y, h, c);
        drawFastVLine(x + w - 1, y, h, c);
    }
    void drawLine(int16_t, int16_t, int16_t, int16_t, uint16_t) {}
    void drawCircle(int16_t, int16_t, int16_t, uint16_t) {}
    void fillCircle(int16_t, int16_t, int16_t, uint16_t) {}
    void fillTriangle(int16_t, int16_t, int16_t, int16_t, int16_t, int16_t, uint16_t) {}

    void setFont(const GFXfont* = nullptr) {}
    void setTextSize(uint8_t s) { _ts = s; }
    void setTextColor(uint16_t) {}
    void setCursor(int16_t x, int16_t y) { _cx = x; _cy = y; }
    void getTextBounds(const char* s, int16_t x, int16_t y,
                       int16_t* x1, int16_t* y1, uint16_t* w, uint16_t* h) {
        *x1 = x; *y1 = y;
        *w = (uint16_t)(strlen(s) * 6 * _ts);
        *h = (uint16_t)(8 * _ts);
    }
    void print(const char*) {}
    void print(char) {}
    void print(int) {}
    void print(unsigned int) {}
    void print(long) {}
    void print(unsigned long) {}

private:
    int16_t _w, _h;
    uint16_t* _buf;
    uint8_t _ts = 1;
    int16_t _cx = 0, _cy = 0;
};
