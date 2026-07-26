#ifndef SPRITE_MANAGER_H
#define SPRITE_MANAGER_H

#include <Adafruit_GFX.h>

// Colour lookup characters
#define SP_K 0x0000  // blacK
#define SP_W 0xFFFF  // White
#define SP_M 0xF81F  // Magenta
#define SP_G 0x07E0  // Green
#define SP_R 0xF800  // Red
#define SP_Y 0xFFE0  // Yellow
#define SP_A 0x001F  // Azure/Blue
#define SP_O 0xFD20  // Orange
#define SP_C 0x07FF  // Cyan
#define SP_X 0x0000  // transparent (skip pixel)

static const int SPRITE_SIZE = 8;

struct SpriteFrame {
    uint16_t pixels[SPRITE_SIZE][SPRITE_SIZE];
};

struct AnimatedSprite {
    const SpriteFrame* frames;
    int frameCount;
    unsigned long frameDurationMs;
};

class SpriteManager {
public:
    static void drawSprite(GFXcanvas16 &canvas, const SpriteFrame &frame, int x, int y) {
        for (int row = 0; row < SPRITE_SIZE; row++) {
            for (int col = 0; col < SPRITE_SIZE; col++) {
                uint16_t c = frame.pixels[row][col];
                if (c == SP_X) continue;
                canvas.drawPixel(x + col, y + row, c);
            }
        }
    }

    static void drawAnimated(GFXcanvas16 &canvas, const AnimatedSprite &anim, int x, int y) {
        int frameIdx = (millis() / anim.frameDurationMs) % anim.frameCount;
        drawSprite(canvas, anim.frames[frameIdx], x, y);
    }
};

#endif // SPRITE_MANAGER_H