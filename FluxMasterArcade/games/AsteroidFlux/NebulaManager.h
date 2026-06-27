#ifndef NEBULA_MANAGER_H
#define NEBULA_MANAGER_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../../cabinet/ArcadeConfig.h"

class NebulaManager {
private:
    struct CloudNode {
        float x;
        int y;
    };

    static const int POINTS_PER_CLOUD = 4;
    CloudNode _cloudA[POINTS_PER_CLOUD];
    CloudNode _cloudB[POINTS_PER_CLOUD];

public:
    NebulaManager() {
        // Space them apart initially so they don't spawn right on top of each other
        resetCloud(_cloudA, 0);
        resetCloud(_cloudB, ArcadeConfig::SCREEN_WIDTH * 0.65f); 
    }

    void resetCloud(CloudNode cloud[], int startingX) {
        // Decreasing the horizontal step multiplier to compress the cloud width
        int compressedWidthStep = 18; 

        for (int i = 0; i < POINTS_PER_CLOUD; i++) {
            cloud[i].x = startingX + (i * compressedWidthStep);
            
            // Constrain heights to a tight 40-pixel vertical envelope in the center half of space
            cloud[i].y = random(50, 90); 
        }
    }

    void update() {
        // Scroll Cloud A
        for (int i = 0; i < POINTS_PER_CLOUD; i++) {
            _cloudA[i].x -= ArcadeConfig::NEBULA_SCROLL_SPEED;
        }
        // Recycle once the tail end of the compact structure drops off-screen
        if (_cloudA[POINTS_PER_CLOUD - 1].x < -10) {
            // Respawn safely out of view to the right with a minor random layout variance
            resetCloud(_cloudA, ArcadeConfig::SCREEN_WIDTH + random(10, 40));
        }

        // Scroll Cloud B
        for (int i = 0; i < POINTS_PER_CLOUD; i++) {
            _cloudB[i].x -= ArcadeConfig::NEBULA_SCROLL_SPEED;
        }
        if (_cloudB[POINTS_PER_CLOUD - 1].x < -10) {
            resetCloud(_cloudB, ArcadeConfig::SCREEN_WIDTH + random(10, 40));
        }
    }

    void render(GFXcanvas16 &canvas) {
        uint16_t nebulaColor = ArcadeConfig::COLOR_NEBULA;

        // Draw Cloud A vector lines connecting nodes
        for (int i = 0; i < POINTS_PER_CLOUD - 1; i++) {
            // Skip rendering lines that are completely off screen bounds to optimize ticks
            if (_cloudA[i].x > ArcadeConfig::SCREEN_WIDTH) continue;
            
            canvas.drawLine((int)_cloudA[i].x, _cloudA[i].y, (int)_cloudA[i+1].x, _cloudA[i+1].y, nebulaColor);
            if (i < POINTS_PER_CLOUD - 2) {
                canvas.drawLine((int)_cloudA[i].x, _cloudA[i].y, (int)_cloudA[i+2].x, _cloudA[i+2].y, nebulaColor);
            }
        }

        // Draw Cloud B vector lines
        for (int i = 0; i < POINTS_PER_CLOUD - 1; i++) {
            if (_cloudB[i].x > ArcadeConfig::SCREEN_WIDTH) continue;

            canvas.drawLine((int)_cloudB[i].x, _cloudB[i].y, (int)_cloudB[i+1].x, _cloudB[i+1].y, nebulaColor);
            if (i < POINTS_PER_CLOUD - 2) {
                canvas.drawLine((int)_cloudB[i].x, _cloudB[i].y, (int)_cloudB[i+2].x, _cloudB[i+2].y, nebulaColor);
            }
        }
    }
};

#endif