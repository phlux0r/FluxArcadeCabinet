#ifndef SHIP_H
#define SHIP_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../../cabinet/ArcadeConfig.h"
#include "../../cabinet/ParticleManager.h"

// =============================================================================
// LANDER FLUX — SHIP
// Physics are per-step constants, no delta-time: the engine runs them in
// fixed 20ms steps (50 a second), whatever the frame rate.
//
// The engine spools: A held brings it up to full over SPOOL_UP_STEPS, let
// go it dies away over SPOOL_DOWN_STEPS, so a tap gives a nudge rather than
// a full kick. The ship turns towards the angle the stick asks for at
// TURN_PER_STEP, not snapping to it.
// =============================================================================

class Ship {
public:
    float x, y;
    float vx, vy;
    float thrustAngle;
    float engine = 0.0f;              // 0 (off) to 1 (full thrust)
    static constexpr float SPOOL_UP_STEPS   = 7.5f;    // 150ms to full
    static constexpr float SPOOL_DOWN_STEPS = 5.0f;    // 100ms to off
    static constexpr float TURN_PER_STEP    = PI / 50.0f;   // 180 degrees a second
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
        thrustAngle = 0.0f;
        engine = 0.0f;
        isDisintegrating = false;
    }

    void kill(ParticleManager &particles) {
        lives--;
        isDisintegrating   = true;
        explosionStartTime = millis();
        particles.triggerExplosion(x, y, 60, 4);
    }

    // One physics step (20ms). targetAngle is where the stick points
    // (radians, 0 = upright); thrustPower is the engine at full.
    void updatePhysics(bool isThrusterFiring, float targetAngle,
                       float gravity, float thrustPower,
                       ParticleManager &particles) {
        thrustAngle += constrain(targetAngle - thrustAngle, -TURN_PER_STEP, TURN_PER_STEP);
        const bool firing = isThrusterFiring && fuel > 0.0f;
        engine = firing ? min(1.0f, engine + 1.0f / SPOOL_UP_STEPS)
                        : max(0.0f, engine - 1.0f / SPOOL_DOWN_STEPS);
        vy += gravity;

        if (engine > 0.0f) {
            vx += thrustPower * engine * sin(thrustAngle);
            vy -= thrustPower * engine * cos(thrustAngle);
            fuel -= 0.4f * engine;           // burns as hard as it pushes
            if (fuel < 0.0f) fuel = 0.0f;
        }
        if (firing) {
            if (random(0, 10) > 2) {
                float fireVX = -sin(thrustAngle) * 1.5f + (random(-3, 3) * 0.1f);
                float fireVY =  cos(thrustAngle) * 1.5f + (random(0, 3)  * 0.1f);
                particles.spawnFire(x, y + 4, fireVX, fireVY, 0, 2);   // a short plume
            }
        }

        x += vx;
        y += vy;

        if (x < 3) { x = 3; vx = 0; }
        if (x > ArcadeConfig::PORTRAIT_WIDTH - 3) { x = ArcadeConfig::PORTRAIT_WIDTH - 3; vx = 0; }
        if (y < 4) { y = 4; vy = 0; }
    }

    // safeToLand: whether touching down now would be a landing (the hull
    // flashes green near the ground if so, red if not).
    void render(GFXcanvas16 &canvas, bool isThrusterFiring, bool safeToLand) {
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
            if (safeToLand) {
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