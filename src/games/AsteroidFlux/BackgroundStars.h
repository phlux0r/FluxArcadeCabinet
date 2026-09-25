#ifndef BACKGROUND_STARS_H
#define BACKGROUND_STARS_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../../cabinet/ArcadeConfig.h"

class BackgroundStars {
private:
    struct StarData {
        float x;
        int y;
    };

    StarData _stars[ArcadeConfig::MAX_STARS];

public:
    BackgroundStars() {
        // Distribute stars evenly across the initial screen buffer canvas width on boot
        for (int i = 0; i < ArcadeConfig::MAX_STARS; i++) {
            _stars[i].x = random(0, ArcadeConfig::SCREEN_WIDTH);
            _stars[i].y = random(ArcadeConfig::UI_MARGIN_TOP + 2, ArcadeConfig::SCREEN_HEIGHT - 2);
        }
    }

    void update() {
        for (int i = 0; i < ArcadeConfig::MAX_STARS; i++) {
            // Apply slow parallax scroll physics vector
            _stars[i].x -= ArcadeConfig::STAR_SCROLL_SPEED;

            // Recycle star to the right edge when it slips out of view
            if (_stars[i].x < 0) {
                _stars[i].x = ArcadeConfig::SCREEN_WIDTH;
                _stars[i].y = random(ArcadeConfig::UI_MARGIN_TOP + 2, ArcadeConfig::SCREEN_HEIGHT - 2);
            }
        }
    }

    void render(GFXcanvas16 &canvas) {
        for (int i = 0; i < ArcadeConfig::MAX_STARS; i++) {
            // Render basic 1-pixel stars
            canvas.drawPixel((int)_stars[i].x, _stars[i].y, ST7735_WHITE);
        }
    }
};

#endif