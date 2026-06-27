#ifndef SHIP_H
#define SHIP_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../../cabinet/ArcadeConfig.h"
#include "../../cabinet/ParticleManager.h"

// =============================================================================
// LANDER FLUX — SHIP
// Physics are original per-frame constants — no delta-time.
// The engine throttles to 50fps (20ms/frame) so these constants produce
// identical behaviour to the original standalone delay(20) loop.
// =============================================================================

class Ship {
public:
    float x, y;
    float vx, vy;
    float thrustAngle;
    int   lives;
    float fuel;
    bool  isDisintegrating;
    unsigned long explosionStartTime;

    float noseLength = 8.0f;
    float wingWidth  = 4.0f;
    float wingSweep  = 4.0f;
    float jetOffset  = 1.0f;

    Ship() {
        lives = 3;
        fuel  = 100.0f;
        isDisintegrating = false;
        thrustAngle = 0.0f;
        vx = vy = 0.0f;
        x = y = 0.0f;
    }

    void resetPools() {
        lives = 3;
        fuel  = 100.0f;
        isDisintegrating = false;
    }

    void spawn() {
        x  = ArcadeConfig::PORTRAIT_WIDTH / 2.0f;
        y  = 10.0f;
        vx = 0.0f;
        vy = 0.0f;
        fuel = 100.0f;
        isDisintegrating = false;
    }

    void kill(ParticleManager &particles) {
        lives--;
        isDisintegrating   = true;
        explosionStartTime = millis();
        particles.triggerExplosion(x, y);
    }

    // Original per-frame physics — called only when engine timer fires (~50fps)
    void updatePhysics(bool isThrusterFiring, float dialAngle,
                       float gravity, float thrustPower,
                       ParticleManager &particles) {
        thrustAngle = dialAngle;
        vy += gravity;

        if (isThrusterFiring && fuel > 0.0f) {
            vx += thrustPower * sin(thrustAngle);
            vy -= thrustPower * cos(thrustAngle);
            fuel -= 0.4f;
            if (fuel < 0.0f) fuel = 0.0f;

            if (random(0, 10) > 2) {
                float fireVX = -sin(thrustAngle) * 1.5f + (random(-3, 3) * 0.1f);
                float fireVY =  cos(thrustAngle) * 1.5f + (random(0, 3)  * 0.1f);
                particles.spawnFire(x, y + 4, fireVX, fireVY);
            }
        }

        x += vx;
        y += vy;

        if (x < 3) { x = 3; vx = 0; }
        if (x > ArcadeConfig::PORTRAIT_WIDTH - 3) { x = ArcadeConfig::PORTRAIT_WIDTH - 3; vx = 0; }
        if (y < 4) { y = 4; vy = 0; }
    }

    void render(GFXcanvas16 &canvas, bool isThrusterFiring, float safeSpeedThreshold) {
        if (isDisintegrating) return;

        float cosA = cos(thrustAngle);
        float sinA = sin(thrustAngle);

        float localTip[2]   = { 0.0f,      -noseLength };
        float localLeft[2]  = { -wingWidth,  wingSweep  };
        float localJet[2]   = { 0.0f,        jetOffset  };
        float localRight[2] = {  wingWidth,   wingSweep  };

        int pTipX   = x + (localTip[0]   * cosA - localTip[1]   * sinA);
        int pTipY   = y + (localTip[0]   * sinA + localTip[1]   * cosA);
        int pLeftX  = x + (localLeft[0]  * cosA - localLeft[1]  * sinA);
        int pLeftY  = y + (localLeft[0]  * sinA + localLeft[1]  * cosA);
        int pJetX   = x + (localJet[0]   * cosA - localJet[1]   * sinA);
        int pJetY   = y + (localJet[0]   * sinA + localJet[1]   * cosA);
        int pRightX = x + (localRight[0] * cosA - localRight[1] * sinA);
        int pRightY = y + (localRight[0] * sinA + localRight[1] * cosA);

        uint16_t hullOutlineColor = ArcadeConfig::COLOR_MAGENTA;
        uint16_t hullFillColor    = ArcadeConfig::COLOR_CYAN;

        if (y >= (ArcadeConfig::PORTRAIT_HEIGHT - 45)) {
            float totalSpeed = sqrt((vx * vx) + (vy * vy));
            if (totalSpeed < safeSpeedThreshold) {
                hullOutlineColor = (millis() % 300 < 150) ? ArcadeConfig::COLOR_GREEN   : ArcadeConfig::COLOR_MAGENTA;
            } else {
                hullOutlineColor = (millis() % 300 < 150) ? ArcadeConfig::COLOR_RED     : ArcadeConfig::COLOR_MAGENTA;
            }
        }

        canvas.fillTriangle(pTipX, pTipY, pLeftX,  pLeftY,  pJetX, pJetY, hullFillColor);
        canvas.fillTriangle(pTipX, pTipY, pRightX, pRightY, pJetX, pJetY, hullFillColor);
        canvas.drawLine(pTipX,   pTipY,   pLeftX,  pLeftY,  hullOutlineColor);
        canvas.drawLine(pLeftX,  pLeftY,  pJetX,   pJetY,   hullOutlineColor);
        canvas.drawLine(pJetX,   pJetY,   pRightX, pRightY, hullOutlineColor);
        canvas.drawLine(pRightX, pRightY, pTipX,   pTipY,   hullOutlineColor);

        int vectorLineEndX = x + (sin(thrustAngle) * 9);
        int vectorLineEndY = y - (cos(thrustAngle) * 9);
        uint16_t indicatorColor = isThrusterFiring ? ArcadeConfig::COLOR_YELLOW : hullOutlineColor;
        canvas.drawLine((int)x, (int)y, vectorLineEndX, vectorLineEndY, indicatorColor);
    }
};

#endif // SHIP_H