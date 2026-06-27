#ifndef GAME_ENGINE_LANDER_H
#define GAME_ENGINE_LANDER_H

#include <Adafruit_GFX.h>
#include <Preferences.h>
#include "../../cabinet/ArcadeConfig.h"
#include "../../cabinet/ParticleManager.h"
#include "../../cabinet/AudioEngine.h"
#include "CavernObstacles.h"
#include "Ship.h"
#include "assets/TitleScreen.h"
#include "../../assets/shared/SharedAssets.h"

// =============================================================================
// GAME ENGINE — LANDER FLUX
//
// PHYSICS APPROACH: Simple 50fps throttle matching original delay(20).
// update() is called at 60fps by the framework but physics only step when
// 20ms have elapsed since the last physics tick. This means the original
// per-frame constants (gravity=0.04, thrust=0.09, fuel-=0.4) produce
// identical behaviour to the standalone version. No delta-time needed.
//
// TITLE SCREEN: Bitmap is blitted to TFT only when blink state changes
// (once per 600ms) — not every frame — so the startup melody doesn't
// cause visible re-rendering on every note.
// =============================================================================

// Physics tick interval — matches original delay(20) = 50fps
static const unsigned long PHYSICS_TICK_MS = 20UL;

class GameEngineLander {
private:
    Preferences      _prefs;
    Adafruit_ST7735* _tft = nullptr;
    CavernObstacles  _obstacles;
    ParticleManager  _particles;
    Ship             _lander;

    const float THRUST_POWER       = 0.09f;
    const float SAFE_LANDING_SPEED = 1.1f;

    int   _score         = 0;
    int   _level         = 1;
    int   _highScore     = 0;
    bool  _isGameOver    = false;
    bool  _isTitleScreen = true;

    unsigned long _attractModeTimer    = 0;
    bool          _showInstructionPage = false;

    float _currentGravity  = 0.04f;
    bool  _fuelTankActive  = false;
    float _fuelTankX       = 0.0f;
    float _fuelTankY       = 0.0f;
    const int _fuelTankRadius = 4;

    static const int GROUND_SEGMENTS = 9;
    int _groundY[GROUND_SEGMENTS];
    int _groundStepX;
    int _padX;
    const int _padWidth = 24;

    // Button B release guard
    bool _btnBWasHeld = false;

    // Game-over attract timeout
    unsigned long _gameOverEnteredMs = 0;
    static const unsigned long GAMEOVER_TIMEOUT_MS = 30000UL;

    // Physics throttle — only tick physics every 20ms (matches original delay(20))
    unsigned long _lastPhysicsTick = 0;

    bool _thrustSoundActive = false;

    void saveHighScore() {
        _prefs.begin("lander_flux", false);
        _prefs.putInt("high_score", _highScore);
        _prefs.end();
    }

    void initLevel() {
        _lander.spawn();
        _padX = random(15, ArcadeConfig::PORTRAIT_WIDTH - 15 - _padWidth);

        int gravityIncrements = (_level - 1) / 3;
        _currentGravity = 0.04f + (gravityIncrements * 0.01f);
        if (_currentGravity > 0.10f) _currentGravity = 0.10f;

        _obstacles.generateNewMap(_level);

        // Safe-spawn fuel tank — original 50-attempt collision check
        if (gravityIncrements >= 2) {
            _fuelTankActive = true;
            bool safeSpawnFound = false;
            int attempts = 0;
            while (!safeSpawnFound && attempts < 50) {
                attempts++;
                _fuelTankX = random(20, ArcadeConfig::PORTRAIT_WIDTH  - 20);
                _fuelTankY = random(45, ArcadeConfig::PORTRAIT_HEIGHT - 50);
                bool blocked = false;
                for (int testY = _fuelTankY;
                     testY < ArcadeConfig::PORTRAIT_HEIGHT - 20; testY += 4) {
                    if (_obstacles.checkCollision(_fuelTankX, testY, _fuelTankRadius + 2)) {
                        blocked = true; break;
                    }
                }
                if (!blocked) safeSpawnFound = true;
            }
            if (!safeSpawnFound) _fuelTankActive = false;
        } else {
            _fuelTankActive = false;
        }

        // Ground layout
        _groundStepX = ArcadeConfig::PORTRAIT_WIDTH / (GROUND_SEGMENTS - 1);
        int baselineFloorY = ArcadeConfig::PORTRAIT_HEIGHT - 10;
        for (int i = 0; i < GROUND_SEGMENTS; i++) {
            int segmentX = i * _groundStepX;
            if (segmentX >= (_padX - 8) && segmentX <= (_padX + _padWidth + 8)) {
                _groundY[i] = baselineFloorY;
            } else {
                _groundY[i] = baselineFloorY - random(-7, 7);
            }
        }

        _lastPhysicsTick  = millis();
        _thrustSoundActive = false;
    }

    // Draw title bitmap into canvas, overlay blinking prompt — same pattern as Asteroid Flux.
    // Canvas is flushed to TFT by the single blit at the bottom of update().
    void renderTitleScreen(GFXcanvas16 &canvas) {
        // Blit the 128x160 portrait bitmap into the canvas pixel by pixel
        for (int i = 0; i < (ArcadeConfig::PORTRAIT_WIDTH * ArcadeConfig::PORTRAIT_HEIGHT); i++) {
            uint16_t px = pgm_read_word(&lander_flux_128x160_data[i]);
            canvas.drawPixel(i % ArcadeConfig::PORTRAIT_WIDTH,
                             i / ArcadeConfig::PORTRAIT_WIDTH, px);
        }
        // Blinking prompt — visible 600ms out of every 1000ms
        if (millis() % 1000 < 600) {
            canvas.setTextSize(1);
            canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
            canvas.setCursor(10, 145);
            canvas.print("HIT BUTTON TO START");
        }
    }

    void renderInstructionScreen(GFXcanvas16 &canvas) {
        canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
        canvas.setTextSize(2);
        canvas.setCursor(4, 12);
        canvas.setTextColor(ArcadeConfig::COLOR_MAGENTA);
        canvas.print("HOW TO FLY");

        canvas.setTextSize(1);
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.setCursor(1, 40);  canvas.print("> JOYSTICK STEERS");
        canvas.setCursor(1, 52);  canvas.print("> BTN A FIRES BOOST");
        canvas.setCursor(1, 64);  canvas.print("> WATCH FUEL GAUGE");
        canvas.setCursor(1, 76);  canvas.print("> PICK UP FUEL CORES");
        canvas.setCursor(1, 88);  canvas.print("> LAND SLOW ON PAD");

        canvas.drawRect(4, 106,
            ArcadeConfig::PORTRAIT_WIDTH - 12, 28, ArcadeConfig::COLOR_ION_BLUE);
        canvas.setCursor(10, 115);
        canvas.setTextColor(ArcadeConfig::COLOR_YELLOW);
        canvas.print("HIGH SCORE: ");
        canvas.setTextColor(ArcadeConfig::COLOR_GREEN);
        canvas.print(_highScore);
    }

    void renderSuccessIntermission(GFXcanvas16 &canvas) {
        canvas.fillScreen(ArcadeConfig::COLOR_GREEN);
        canvas.setCursor(20, 55);
        canvas.setTextSize(1);
        canvas.setTextColor(ArcadeConfig::COLOR_BLACK);
        canvas.print("PERFECT LANDING!");
        canvas.setCursor(30, 70);
        canvas.print("NEXT LEVEL: "); canvas.print(_level);
        canvas.setCursor(12, 110);
        canvas.print("[BTN A] CONTINUE");
    }

    void renderGameOverIntermission(GFXcanvas16 &canvas) {
        canvas.fillScreen(ArcadeConfig::COLOR_RED);
        canvas.setCursor(10, 45);
        canvas.setTextSize(2);
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.print("GAME OVER");

        canvas.setTextSize(1);
        canvas.setCursor(19, 75);
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.print("FINAL SCORE: "); canvas.print(_score);

        canvas.setCursor(7, 115);
        canvas.setTextColor(ArcadeConfig::COLOR_YELLOW);
        canvas.print("[BTN A] MAIN MENU");

        unsigned long elapsed = millis() - _gameOverEnteredMs;
        if (elapsed > (GAMEOVER_TIMEOUT_MS - 10000UL)) {
            int secsLeft = (int)((GAMEOVER_TIMEOUT_MS - elapsed) / 1000UL) + 1;
            canvas.setCursor(35, 130);
            canvas.setTextColor(ArcadeConfig::COLOR_YELLOW);
            canvas.print("AUTO: "); canvas.print(secsLeft); canvas.print("s");
        }
    }

public:
    GameEngineLander() {}

    void setTFT(Adafruit_ST7735 &tft) { _tft = &tft; }

    void init(AudioEngine &audio) {
        _prefs.begin("lander_flux", true);
        _highScore = _prefs.getInt("high_score", 0);
        _prefs.end();

        _score               = 0;
        _level               = 1;
        _lander.resetPools();
        _particles.clearAll();
        _isTitleScreen       = true;
        _isGameOver          = false;
        _attractModeTimer    = millis();
        _showInstructionPage = false;
        _btnBWasHeld         = true;
        initLevel();
        audio.playLanderStartSound();
    }

    bool update(GFXcanvas16 &canvas, bool btnA, bool btnB,
                int joyX, int joyY, AudioEngine &audio) {

        // Button B release guard
        if (_btnBWasHeld) {
            if (!btnB) _btnBWasHeld = false;
        } else if (btnB) {
            audio.mute();
            return false;
        }

        // ---- ATTRACT / TITLE SCREEN ----
        if (_isTitleScreen) {
            // Rotate slides every 8 seconds
            if (millis() - _attractModeTimer > 8000UL) {
                _showInstructionPage = !_showInstructionPage;
                _attractModeTimer    = millis();
            }

            // Draw to canvas — single blit at bottom handles flush
            if (!_showInstructionPage) renderTitleScreen(canvas);
            else                       renderInstructionScreen(canvas);

            if (btnA) {
                _score = 0; _level = 1;
                _lander.resetPools();
                _particles.clearAll();
                initLevel();
                _isTitleScreen = false;
                _isGameOver    = false;
                audio.playLaunchMelody();
            }

            // Single canvas flush — exactly like Asteroid Flux
            if (_tft) _tft->drawRGBBitmap(0, 0, canvas.getBuffer(),
                ArcadeConfig::PORTRAIT_WIDTH, ArcadeConfig::PORTRAIT_HEIGHT);
            return true;
        }

        // ---- GAME OVER / SUCCESS INTERMISSION ----
        if (_isGameOver) {
            if (_lander.lives > 0) {
                renderSuccessIntermission(canvas);
            } else {
                renderGameOverIntermission(canvas);
            }
            if (_tft) _tft->drawRGBBitmap(0, 0, canvas.getBuffer(),
                ArcadeConfig::PORTRAIT_WIDTH, ArcadeConfig::PORTRAIT_HEIGHT);

            if (btnA) {
                if (_lander.lives > 0) {
                    initLevel();
                } else {
                    _lander.resetPools();
                    _particles.clearAll();
                    initLevel();
                    _isTitleScreen    = true;
                    _attractModeTimer = millis();
                    _showInstructionPage = false;
                }
                _isGameOver = false;
                return true;
            }

            // 30-second timeout → attract mode
            if (millis() - _gameOverEnteredMs > GAMEOVER_TIMEOUT_MS) {
                _lander.resetPools();
                _particles.clearAll();
                initLevel();
                _isTitleScreen       = true;
                _isGameOver          = false;
                _attractModeTimer    = millis();
                _showInstructionPage = false;
                audio.playLanderStartSound();
            }
            return true;
        }

        // ---- PHYSICS TICK (50fps throttle) ----
        // Only step physics when 20ms have elapsed — matches original delay(20)
        unsigned long now = millis();
        bool physicsTick = (now - _lastPhysicsTick >= PHYSICS_TICK_MS);
        if (physicsTick) _lastPhysicsTick = now;

        if (_lander.isDisintegrating) {
            if (physicsTick) _particles.update();
            if (now - _lander.explosionStartTime > 1200) {
                if (_lander.lives > 0) {
                    initLevel();
                } else {
                    if (_score > _highScore) { _highScore = _score; saveHighScore(); }
                    _isGameOver        = true;
                    _gameOverEnteredMs = now;
                    audio.playGameOverSound(gameend_data, sizeof(gameend_data));
                }
            }
        } else if (physicsTick) {
            float targetAngle = map(joyX, 0, 4095, -45, 45) * (PI / 180.0f);
            _lander.updatePhysics(btnA, targetAngle, _currentGravity,
                                  THRUST_POWER, _particles);

            // Thrust sound — once per press
            if (btnA && _lander.fuel > 0.0f) {
                if (!_thrustSoundActive) {
                    audio.playThrustTick();
                    _thrustSoundActive = true;
                }
            } else {
                _thrustSoundActive = false;
            }

            // Fuel tank pickup
            if (_fuelTankActive) {
                float dx = _lander.x - _fuelTankX;
                float dy = _lander.y - _fuelTankY;
                int ir = 6 + _fuelTankRadius;
                if ((dx*dx + dy*dy) < (ir * ir)) {
                    _fuelTankActive = false;
                    _lander.fuel = min(100.0f, _lander.fuel + 40.0f);
                    for (int p = 0; p < 15; p++) {
                        _particles.spawnFire(_fuelTankX, _fuelTankY,
                            random(-10, 10) * 0.1f, random(-10, 10) * 0.1f);
                    }
                    audio.playPowerUpExtraLife();
                }
            }

            // Obstacle collision
            if (_obstacles.checkCollision(_lander.x, _lander.y, 4)) {
                _lander.kill(_particles);
                audio.playExplosionSound(explosion_data, sizeof(explosion_data));
            }

            // Ground collision
            int segIdx = (int)_lander.x / _groundStepX;
            if (segIdx >= GROUND_SEGMENTS - 1) segIdx = GROUND_SEGMENTS - 2;
            int segStartX = segIdx * _groundStepX;
            float pct = (float)((int)_lander.x - segStartX) / (float)_groundStepX;
            int floorY = _groundY[segIdx]
                       + (int)(pct * (_groundY[segIdx + 1] - _groundY[segIdx]));

            if (_lander.y >= floorY - 4) {
                _lander.y = floorY - 4;
                float speed = sqrt(_lander.vx * _lander.vx + _lander.vy * _lander.vy);
                bool overPad = ((int)_lander.x >= _padX &&
                                (int)_lander.x <= (_padX + _padWidth));
                if (overPad && speed < SAFE_LANDING_SPEED && _lander.fuel > 0.0f) {
                    _score += (int)_lander.fuel;
                    if (_score > _highScore) { _highScore = _score; saveHighScore(); }
                    _level++;
                    _isGameOver        = true;
                    _gameOverEnteredMs = now;
                    audio.playLandingSuccessSound();
                } else {
                    _lander.kill(_particles);
                    audio.playExplosionSound(explosion_data, sizeof(explosion_data));
                }
            }
        }

        // ---- RENDER (every frame regardless of physics tick) ----
        canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
        _obstacles.render(canvas);
        _particles.update();
        _particles.render(canvas);

        if (_fuelTankActive) {
            canvas.drawTriangle(_fuelTankX, _fuelTankY - 5,
                                _fuelTankX - 4, _fuelTankY + 1,
                                _fuelTankX + 4, _fuelTankY + 1,
                                ArcadeConfig::COLOR_ION_BLUE);
            canvas.drawTriangle(_fuelTankX, _fuelTankY + 5,
                                _fuelTankX - 4, _fuelTankY - 1,
                                _fuelTankX + 4, _fuelTankY - 1,
                                ArcadeConfig::COLOR_ION_BLUE);
            canvas.fillCircle(_fuelTankX, _fuelTankY, 2, ArcadeConfig::COLOR_YELLOW);
        }

        // Ground terrain
        for (int xs = 0; xs < ArcadeConfig::PORTRAIT_WIDTH; xs++) {
            int seg = xs / _groundStepX;
            if (seg >= GROUND_SEGMENTS - 1) seg = GROUND_SEGMENTS - 2;
            int sx = seg * _groundStepX;
            float p = (float)(xs - sx) / (float)_groundStepX;
            int ey = _groundY[seg] + (int)(p * (_groundY[seg+1] - _groundY[seg]));
            canvas.drawFastVLine(xs, ey,
                ArcadeConfig::PORTRAIT_HEIGHT - ey, 0x9300);
            canvas.drawPixel(xs, ey, 0x4100);
        }

        // Landing pad (styled with grid lines — original)
        int padY = ArcadeConfig::PORTRAIT_HEIGHT - 10;
        canvas.drawRect(_padX, padY - 2, _padWidth, 4, ArcadeConfig::COLOR_GREEN);
        canvas.fillRect(_padX + 2, padY - 1, _padWidth - 4, 2, ArcadeConfig::COLOR_BLACK);
        for (int gx = _padX + 4; gx < _padX + _padWidth; gx += 6) {
            canvas.drawFastVLine(gx, padY - 1, 2, ArcadeConfig::COLOR_ION_BLUE);
        }

        _lander.render(canvas, btnA, SAFE_LANDING_SPEED);

        // HUD
        int gravityTier = ((_level - 1) / 3) + 1;
        int fuelPercent = constrain((int)_lander.fuel, 0, 100);

        canvas.setTextSize(1);
        canvas.setCursor(1, 4);
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.print("S:"); canvas.print(_score);

        canvas.setCursor(38, 4);
        if (_lander.fuel < 30.0f && (millis() % 200 < 100)) {
            canvas.setTextColor(ArcadeConfig::COLOR_RED);
        } else {
            canvas.setTextColor(ArcadeConfig::COLOR_GREEN);
        }
        canvas.print("F:"); canvas.print(fuelPercent); canvas.print("%");

        canvas.setCursor(78, 4);
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.print("L:"); canvas.print(_lander.lives);

        canvas.setCursor(104, 4);
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.print("G:"); canvas.print(gravityTier);

        if (_tft) _tft->drawRGBBitmap(0, 0, canvas.getBuffer(),
            ArcadeConfig::PORTRAIT_WIDTH, ArcadeConfig::PORTRAIT_HEIGHT);

        return true;
    }
};

#endif // GAME_ENGINE_LANDER_H