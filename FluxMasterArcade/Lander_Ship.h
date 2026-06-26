#ifndef LANDER_SHIP_H
#define LANDER_SHIP_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "ArcadeConfig.h"
#include "Lander_ParticleEngine.h"

class Lander_Ship {
public:
    float x, y;
    float vx, vy;
    float thrustAngle;
    int lives;
    float fuel; 
    bool isDisintegrating;
    unsigned long explosionStartTime;

    float noseLength = 8.0f;   
    float wingWidth  = 4.0f;   
    float wingSweep  = 4.0f;   
    float jetOffset  = 1.0f;   

    Lander_Ship() {
        lives = 3;
        fuel = 100.0f;
        isDisintegrating = false;
        thrustAngle = 0.0f;
    }

    void resetPools() {
        lives = 3;
        fuel = 100.0f;
        isDisintegrating = false;
    }

    void spawn() {
        x = ArcadeConfig::SCREEN_WIDTH / 2.0f;
        y = 10.0f;
        vx = 0.0f;
        vy = 0.0f;
        fuel = 100.0f;
        isDisintegrating = false;
        thrustAngle = 0.0f;
    }

    void kill(Lander_ParticleEngine &particles) {
        if (!isDisintegrating) {
            isDisintegrating = true;
            explosionStartTime = millis();
            vx = 0.0f;
            vy = 0.0f;
            lives--;
            particles.triggerExplosion(x, y, 90);
        }
    }

    void updatePhysics(bool isThrusting, float currentJoystickAngle, float gravity, float thrustPower, Lander_ParticleEngine &particles) {
        if (isDisintegrating) return;

        thrustAngle = currentJoystickAngle;

        if (isThrusting && fuel > 0.0f) {
            float forceX = sin(thrustAngle) * thrustPower;
            float forceY = -cos(thrustAngle) * thrustPower;

            vx += forceX;
            vy += forceY;
            fuel -= 0.18f; 
            if (fuel < 0.0f) fuel = 0.0f;

            float fireOriginX = x - sin(thrustAngle) * jetOffset;
            float fireOriginY = y + cos(thrustAngle) * jetOffset;

            for (int i = 0; i < 2; i++) {
                float exhaustVx = -sin(thrustAngle) * random(8, 18) * 0.1f + (random(-5, 5) * 0.05f);
                float exhaustVy =  cos(thrustAngle) * random(8, 18) * 0.1f + (random(-5, 5) * 0.05f);
                particles.spawnFire(fireOriginX, fireOriginY, exhaustVx, exhaustVy);
            }
        }

        vy += gravity;
        x += vx;
        y += vy;

        if (x < 3.0f) { x = 3.0f; vx = 0.0f; }
        if (x > ArcadeConfig::SCREEN_WIDTH - 3.0f) { x = ArcadeConfig::SCREEN_WIDTH - 3.0f; vx = 0.0f; }
    }

    void render(GFXcanvas16 &canvas, bool isThrusting, float safeSpeedThreshold) {
        if (isDisintegrating) return;

        float cosA = cos(thrustAngle);
        float sinA = sin(thrustAngle);

        int pTipX  = x + (sinA * noseLength);
        int pTipY  = y - (cosA * noseLength);
        int pLeftX = x - (cosA * wingWidth)  - (sinA * wingSweep);
        int pLeftY = y - (sinA * wingWidth)  + (cosA * wingSweep);
        int pRightX= x + (cosA * wingWidth)  - (sinA * wingSweep);
        int pRightY= y + (sinA * wingWidth)  + (cosA * wingSweep);
        int pJetX  = x - (sinA * jetOffset);
        int pJetY  = y + (cosA * jetOffset);

        uint16_t hullFillColor = 0x0000; 
        uint16_t hullOutlineColor = ArcadeConfig::COLOR_WHITE;

        // RESTORED FLASHING: Triggers when the ship enters the landing approach zone
        if (y > ArcadeConfig::SCREEN_HEIGHT - 40) {
            float totalSpeed = sqrt((vx * vx) + (vy * vy));
            if (totalSpeed < safeSpeedThreshold) {
                hullOutlineColor = (millis() % 300 < 150) ? ArcadeConfig::COLOR_GREEN : 0xF81F; // Green/Magenta flash
            } else {
                hullOutlineColor = (millis() % 300 < 150) ? 0xF800 : 0xF81F; // Red/Magenta flash
            }
        }

        canvas.fillTriangle(pTipX, pTipY, pLeftX, pLeftY, pJetX, pJetY, hullFillColor);
        canvas.fillTriangle(pTipX, pTipY, pRightX, pRightY, pJetX, pJetY, hullFillColor);

        canvas.drawLine(pTipX, pTipY, pLeftX, pLeftY, hullOutlineColor);   
        canvas.drawLine(pLeftX, pLeftY, pJetX, pJetY, hullOutlineColor);   
        canvas.drawLine(pJetX, pJetY, pRightX, pRightY, hullOutlineColor); 
        canvas.drawLine(pRightX, pRightY, pTipX, pTipY, hullOutlineColor); 

        int vectorLineEndX = x + (sinA * 12.0f);
        int vectorLineEndY = y - (cosA * 12.0f);
        canvas.drawLine(x, y, vectorLineEndX, vectorLineEndY, 0x07FF); // Cyan heading pointer
    }
};

#endif