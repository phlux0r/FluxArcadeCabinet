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

    // safeToLand: whether touching down now would be a landing (the hull's
    // outline flashes green near the ground if so, red if not).
    //
    // A cartoon space-age rocket: a fat teardrop hull with a porthole, an
    // antenna on the nose and two swept fins whose tips are the feet. The
    // fins and nozzle reach y+4, where the engine puts the ground; the
    // engine still collides with rocks at radius 4.
    void render(GFXcanvas16 &canvas, bool isThrusterFiring, bool safeToLand) {
        if (isDisintegrating) return;

        const float cosA = cos(thrustAngle);
        const float sinA = sin(thrustAngle);
        // Local (x, y), y down, rotated about the ship's centre.
        auto px = [&](float lx, float ly) { return (int)lroundf(x + lx * cosA - ly * sinA); };
        auto py = [&](float lx, float ly) { return (int)lroundf(y + lx * sinA + ly * cosA); };

        // Hull outline, clockwise from the nose; convex, so it fills as a
        // fan from the centre.
        static const float HULL[][2] = {
            { 0.0f, -10.0f}, { 1.0f, -9.0f}, { 2.0f, -7.5f}, { 3.0f, -5.5f},
            { 4.0f,  -3.5f}, { 4.5f, -1.0f}, { 4.5f,  1.0f}, { 4.0f,  2.5f},
            { 2.5f,   3.5f},
            {-2.5f,   3.5f}, {-4.0f,  2.5f}, {-4.5f,  1.0f}, {-4.5f, -1.0f},
            {-4.0f,  -3.5f}, {-3.0f, -5.5f}, {-2.0f, -7.5f}, {-1.0f, -9.0f},
        };
        // Right fin (the left mirrors it): from high on the hull's side,
        // swept out and down to a foot.
        static const float FIN[][2] = {
            {3.5f, -4.5f}, {6.5f, 1.0f}, {6.5f, 4.0f}, {3.5f, 2.5f},
        };
        constexpr int HULL_N = sizeof(HULL) / sizeof(HULL[0]);
        constexpr int FIN_N  = sizeof(FIN) / sizeof(FIN[0]);

        uint16_t hullOutlineColor = ArcadeConfig::COLOR_MAGENTA;
        const uint16_t hullFillColor = ArcadeConfig::COLOR_CYAN;
        const uint16_t finColor      = ArcadeConfig::COLOR_ORANGE;

        if (y >= (ArcadeConfig::PORTRAIT_HEIGHT - 45)) {
            if (safeToLand) {
                hullOutlineColor = (millis() % 300 < 150) ? ArcadeConfig::COLOR_GREEN   : ArcadeConfig::COLOR_MAGENTA;
            } else {
                hullOutlineColor = (millis() % 300 < 150) ? ArcadeConfig::COLOR_RED     : ArcadeConfig::COLOR_MAGENTA;
            }
        }

        // Fins first, so the hull overlaps their roots.
        for (int side = -1; side <= 1; side += 2) {
            const int x0 = px(FIN[0][0] * side, FIN[0][1]), y0 = py(FIN[0][0] * side, FIN[0][1]);
            for (int i = 1; i + 1 < FIN_N; i++)
                canvas.fillTriangle(x0, y0,
                                    px(FIN[i][0] * side, FIN[i][1]),     py(FIN[i][0] * side, FIN[i][1]),
                                    px(FIN[i + 1][0] * side, FIN[i + 1][1]), py(FIN[i + 1][0] * side, FIN[i + 1][1]),
                                    finColor);
        }

        int hx[HULL_N], hy[HULL_N];
        for (int i = 0; i < HULL_N; i++) { hx[i] = px(HULL[i][0], HULL[i][1]); hy[i] = py(HULL[i][0], HULL[i][1]); }
        const int cx = (int)lroundf(x), cy = (int)lroundf(y);
        for (int i = 0; i < HULL_N; i++) {
            const int j = (i + 1) % HULL_N;
            canvas.fillTriangle(cx, cy, hx[i], hy[i], hx[j], hy[j], hullFillColor);
        }
        for (int i = 0; i < HULL_N; i++) {
            const int j = (i + 1) % HULL_N;
            canvas.drawLine(hx[i], hy[i], hx[j], hy[j], hullOutlineColor);
        }

        // Nozzle under the hull: grey, lit yellow while the engine fires.
        const uint16_t nozzleColor = isThrusterFiring ? ArcadeConfig::COLOR_YELLOW : ArcadeConfig::COLOR_GREY;
        canvas.fillTriangle(px(-1.5f, 3.5f), py(-1.5f, 3.5f), px(1.5f, 3.5f), py(1.5f, 3.5f),
                            px(0.0f, 4.0f), py(0.0f, 4.0f), nozzleColor);
        canvas.drawLine(px(-1.5f, 4.0f), py(-1.5f, 4.0f), px(1.5f, 4.0f), py(1.5f, 4.0f), nozzleColor);

        // Porthole: a dark rim round a pale glass bubble with a glint.
        const int wx = px(0.0f, -3.0f), wy = py(0.0f, -3.0f);
        canvas.fillCircle(wx, wy, 2, PORTHOLE_RIM);
        canvas.fillCircle(wx, wy, 1, PORTHOLE_GLASS);
        canvas.drawPixel(px(-0.7f, -3.7f), py(-0.7f, -3.7f), ArcadeConfig::COLOR_WHITE);

        // Antenna off the nose, with a bobble on the end.
        canvas.drawLine(hx[0], hy[0], px(0.0f, -12.0f), py(0.0f, -12.0f), ArcadeConfig::COLOR_GREY);
        canvas.drawPixel(px(0.0f, -13.0f), py(0.0f, -13.0f), ArcadeConfig::COLOR_YELLOW);
    }

private:
    static const uint16_t PORTHOLE_RIM   = 0x18C6;   // dark slate
    static const uint16_t PORTHOLE_GLASS = 0x9EFF;   // pale sky blue
};

#endif // SHIP_H