#ifndef ALIEN_SAUCER_H
#define ALIEN_SAUCER_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../../cabinet/ArcadeConfig.h"
#include "../../cabinet/ParticleManager.h"
#include "PlayerShip.h"

// =============================================================================
// ASTEROID FLUX — THE ALIEN SAUCER
// Only while the Fire power-up is on: a pickup brings one in, by chance
// (SAUCER_CHANCE_PCT), a few seconds later. It doesn't pass by: it hangs
// about the right-hand third of the screen, swerving from one random spot
// to the next, and now and then glows red underneath and lobs a plasma
// ball at where the ship is. SAUCER_HITS bolts bring it down for
// SAUCER_SCORE (its arrival tops Fire back up to full, so there's time);
// when Fire runs out it makes off to the right, unscored.
// A plasma ball costs a life as an asteroid would (the shield takes it).
// Movement is per frame, like the asteroids'.
// =============================================================================

class AlienSaucer {
public:
    static const int HALF_W = 8, HALF_H = 4;           // hit box about its centre

    void reset() {
        _state = GONE;
        _dueFrames = 0;
        clearShots();
    }

    void clearShots() { for (auto &p : _shots) p.active = false; }

    // The Fire power-up has just come on: maybe a saucer, a little later.
    void fireStarted() {
        if (_state != GONE || _dueFrames > 0) return;
        if (random(0, 100) >= ArcadeConfig::SAUCER_CHANCE_PCT) return;
        _dueFrames = (int)random(60, 121);              // 2-4s
    }

    // fireOn: whether the Fire power-up is still on (when it ends, the
    // saucer leaves). Returns whether a plasma ball hit the ship unshielded.
    // arrived is set the frame it comes on screen, fired when it shoots.
    bool update(PlayerShip &ship, bool fireOn, ParticleManager &particles,
                bool &arrived, bool &fired, bool &shieldTook) {
        arrived = fired = shieldTook = false;
        if (_state == GONE && _dueFrames > 0) {
            if (!fireOn) _dueFrames = 0;               // Fire's over before it came
            else if (--_dueFrames == 0) arrive(), arrived = true;
        }
        if (_state != GONE) {
            ++_frame;
            if (_flash) --_flash;
            if (_state == HOVER && !fireOn) _state = LEAVING;
            if (_state == ENTERING && !fireOn) _state = LEAVING;
            move();
            if (_state == HOVER && aim(ship)) fired = true;
            if (_state == LEAVING && _x > ArcadeConfig::SCREEN_WIDTH + HALF_W + 2) _state = GONE;
        }
        return moveShots(ship, particles, shieldTook);
    }

    // A bolt that went from x0 to x1 along row y this frame: does it hit
    // the saucer? Each hit flashes it; the last brings it down. Returns
    // 0 for a miss, 1 for a hit, 2 for the one that destroys it.
    int shoot(float x0, float x1, float y, ParticleManager &particles) {
        if (_state == GONE || (_state == ENTERING && _x - HALF_W > ArcadeConfig::SCREEN_WIDTH)) return 0;   // not in view yet
        if (fabsf(y - _y) > HALF_H + 1) return 0;
        if (x1 < _x - HALF_W || x0 > _x + HALF_W) return 0;
        _flash = 6;
        particles.spawnExplosion(_x - HALF_W, y, ArcadeConfig::COLOR_WHITE, 5, 250, 2);
        if (--_hp > 0) return 1;
        particles.spawnExplosion(_x, _y, SAUCER_HULL, 26, 900, 4);
        particles.spawnExplosion(_x, _y, ArcadeConfig::COLOR_CYAN, 14, 700, 4);
        particles.spawnExplosion(_x, _y, ArcadeConfig::COLOR_YELLOW, 10, 500, 3);
        _state = GONE;
        return 2;
    }

    bool onScreen() const { return _state != GONE; }
    bool due() const { return _dueFrames > 0; }
    float x() const { return _x; }
    float y() const { return _y; }
    int hp() const { return _hp; }

    // The plasma balls in flight, for the attract demo's autopilot.
    static const int MAX_SHOTS = 3;
    struct Shot { float x, y, vx, vy; bool active; };
    const Shot* shots() const { return _shots; }

    void render(GFXcanvas16 &canvas) {
        for (const auto &p : _shots) {
            if (!p.active) continue;
            const bool pulse = (_frame / 3) % 2;
            canvas.fillCircle((int)p.x, (int)p.y, 2, pulse ? ArcadeConfig::COLOR_MAGENTA : PLASMA_DARK);
            canvas.drawPixel((int)p.x, (int)p.y, ArcadeConfig::COLOR_WHITE);
        }
        if (_state == GONE) return;
        const int x = (int)_x, y = (int)_y;
        const bool white = _flash > 0 && (_flash % 2);
        // The glass dome, a glint on it.
        canvas.fillCircle(x, y - 2, 4, white ? ArcadeConfig::COLOR_WHITE : SAUCER_GLASS);
        canvas.fillRect(x - 5, y - 1, 11, 3, white ? ArcadeConfig::COLOR_WHITE : SAUCER_HULL);
        canvas.drawPixel(x - 2, y - 4, ArcadeConfig::COLOR_WHITE);
        // The hull: a wide, flat disc, darker underneath.
        canvas.drawFastHLine(x - 8, y + 1, 17, white ? ArcadeConfig::COLOR_WHITE : SAUCER_HULL);
        canvas.drawFastHLine(x - 7, y,     15, white ? ArcadeConfig::COLOR_WHITE : SAUCER_HULL);
        canvas.drawFastHLine(x - 6, y + 2, 13, white ? ArcadeConfig::COLOR_WHITE : SAUCER_DARK);
        canvas.drawFastHLine(x - 4, y + 3,  9, white ? ArcadeConfig::COLOR_WHITE : SAUCER_DARK);
        // Lights chasing round the rim.
        static const uint16_t LIGHTS[3] = { ArcadeConfig::COLOR_YELLOW, ArcadeConfig::COLOR_RED, ArcadeConfig::COLOR_GREEN };
        for (int k = 0; k < 4; k++)
            canvas.drawPixel(x - 6 + k * 4, y + 1, LIGHTS[(k + _frame / 5) % 3]);
        // About to shoot: the port underneath glows.
        if (_charge > 0) {
            canvas.fillRect(x - 1, y + 3, 3, 2, (_charge / 2) % 2 ? ArcadeConfig::COLOR_RED : ArcadeConfig::COLOR_MAGENTA);
        }
    }

private:
    enum State { GONE, ENTERING, HOVER, LEAVING };
    State _state = GONE;
    float _x = 0, _y = 0, _vx = 0, _vy = 0, _tx = 0, _ty = 0;
    int   _hp = 0, _flash = 0, _dueFrames = 0;
    int   _retarget = 0, _nextShot = 0, _charge = 0;
    uint32_t _frame = 0;
    Shot  _shots[MAX_SHOTS];

    static const uint16_t SAUCER_HULL  = 0xAD7A;    // pale steel
    static const uint16_t SAUCER_DARK  = 0x5AEF;
    static const uint16_t SAUCER_GLASS = 0x8F5F;    // pale cyan
    static const uint16_t PLASMA_DARK  = 0xA00A;

    static const int ZONE_X0 = ArcadeConfig::SCREEN_WIDTH * 2 / 3;
    static const int ZONE_X1 = ArcadeConfig::SCREEN_WIDTH - HALF_W - 2;
    static const int ZONE_Y0 = ArcadeConfig::UI_MARGIN_TOP + HALF_H + 4;
    static const int ZONE_Y1 = ArcadeConfig::SCREEN_HEIGHT - HALF_H - 3;
    static constexpr float TOP_SPEED = 1.3f;        // px a frame
    static constexpr float STEER     = 0.09f;       // px a frame, a frame
    static constexpr float SHOT_SPEED = 1.8f;
    static const int CHARGE_FRAMES = 12;            // the glow before a shot

    void arrive() {
        _state = ENTERING;
        _hp = ArcadeConfig::SAUCER_HITS;
        _x = ArcadeConfig::SCREEN_WIDTH + HALF_W + 2;
        _y = (float)random(ZONE_Y0, ZONE_Y1 + 1);
        _vx = -TOP_SPEED; _vy = 0;
        _flash = _charge = 0;
        newTarget();
        _nextShot = (int)random(45, 75);
    }

    void newTarget() {
        _tx = (float)random(ZONE_X0, ZONE_X1 + 1);
        _ty = (float)random(ZONE_Y0, ZONE_Y1 + 1);
        _retarget = (int)random(40, 90);
    }

    // Steers for its target (a new one when it gets there or tires of it),
    // with a little momentum so it swerves rather than jerks.
    void move() {
        float tx = _tx, ty = _ty;
        if (_state == LEAVING) { tx = ArcadeConfig::SCREEN_WIDTH + 40; ty = _y; }
        const float dx = tx - _x, dy = ty - _y;
        const float d = sqrtf(dx * dx + dy * dy) + 0.001f;
        const float want = min(TOP_SPEED, d * 0.08f + 0.3f);
        _vx += constrain(dx / d * want - _vx, -STEER, STEER);
        _vy += constrain(dy / d * want - _vy, -STEER, STEER);
        _x += _vx; _y += _vy;
        _y = constrain(_y, (float)ZONE_Y0, (float)ZONE_Y1);
        if (_state == ENTERING && _x <= ZONE_X1) _state = HOVER;
        if (_state == HOVER && (--_retarget <= 0 || d < 3.0f)) newTarget();
    }

    // Counts down to a shot: CHARGE_FRAMES of glow, then a plasma ball at
    // the ship's middle as it is now. Returns whether it fired.
    bool aim(const PlayerShip &ship) {
        if (_charge > 0) {
            if (--_charge > 0) return false;
            for (auto &p : _shots) {
                if (p.active) continue;
                const float sx = ship.getX() + ArcadeConfig::SHIP_WIDTH / 2.0f;
                const float sy = ship.getY() + ArcadeConfig::SHIP_HEIGHT / 2.0f;
                const float dx = sx - _x, dy = sy - (_y + 4);
                const float d = sqrtf(dx * dx + dy * dy) + 0.001f;
                p = { _x, _y + 4, dx / d * SHOT_SPEED, dy / d * SHOT_SPEED, true };
                _nextShot = (int)random(50, 100);       // 1.7-3.3s till the next
                return true;
            }
            _nextShot = 20;                             // all three still flying
            return false;
        }
        if (--_nextShot <= 0) _charge = CHARGE_FRAMES;
        return false;
    }

    bool moveShots(PlayerShip &ship, ParticleManager &particles, bool &shieldTook) {
        bool hit = false;
        for (auto &p : _shots) {
            if (!p.active) continue;
            p.x += p.vx; p.y += p.vy;
            if (p.x < -3 || p.x > ArcadeConfig::SCREEN_WIDTH + 3 ||
                p.y < ArcadeConfig::UI_MARGIN_TOP || p.y > ArcadeConfig::SCREEN_HEIGHT + 3) {
                p.active = false;
                continue;
            }
            const float sx = ship.getX(), sy = (float)ship.getY();
            if (p.x + 2 >= sx && p.x - 2 <= sx + ArcadeConfig::SHIP_WIDTH &&
                p.y + 2 >= sy && p.y - 2 <= sy + ArcadeConfig::SHIP_HEIGHT) {
                p.active = false;
                if (ship.isShieldActive()) {
                    ship.deactivateShield();
                    particles.spawnExplosion(p.x, p.y, ArcadeConfig::COLOR_CYAN, 12, 500, 3);
                    shieldTook = true;
                } else {
                    hit = true;
                }
            }
        }
        return hit;
    }
};

#endif // ALIEN_SAUCER_H
