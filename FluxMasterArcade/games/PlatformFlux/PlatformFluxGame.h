#ifndef PLATFORM_FLUX_GAME_H
#define PLATFORM_FLUX_GAME_H

#include "../../games/IGame.h"
#include "../../cabinet/ArcadeConfig.h"
#include "../../cabinet/ParticleManager.h"
#include "../../cabinet/AudioEngine.h"

#include "PlayerRunner.h"
#include "PlatformManager.h"
#include "FlyingEnemyManager.h"
#include "RunnerPowerUpManager.h"

#include <Preferences.h>

class PlatformFluxGame : public IGame {
private:
    PlayerRunner          _player;
    PlatformManager       _platforms;
    FlyingEnemyManager    _enemies;
    RunnerPowerUpManager  _powerUp;
    ParticleManager       _particles;

    Preferences      _prefs;
    Adafruit_ST7735* _tft = nullptr;

    int _score     = 0;
    int _highScore = 0;
    int _lastEnemyUnlockTier = 0;
    float _playerXOffset = 0.0f;

    enum GamePhase { PHASE_ATTRACT, PHASE_PLAYING, PHASE_DEATH, PHASE_GAMEOVER };
    GamePhase _phase = PHASE_ATTRACT;

    unsigned long _phaseTimer = 0;
    bool _uiDirty = true;
    bool _btnBWasHeld = false;

    unsigned long _gameOverEnteredMs = 0;
    static const unsigned long GAMEOVER_TIMEOUT_MS = 30000UL;

    void loadHighScore() {
        _prefs.begin("pf_data", true);
        _highScore = _prefs.getInt("highscore", 0);
        _prefs.end();
    }

    void saveHighScore() {
        _prefs.begin("pf_data", false);
        _prefs.putInt("highscore", _highScore);
        _prefs.end();
    }

    void drawUI(GFXcanvas16 &canvas) {
        canvas.fillRect(0, 0, ArcadeConfig::LANDSCAPE_WIDTH, 10, ArcadeConfig::COLOR_BLACK);
        canvas.drawFastHLine(0, 10, ArcadeConfig::LANDSCAPE_WIDTH, ArcadeConfig::COLOR_GREEN);

        canvas.setTextSize(1);
        canvas.setTextColor(ArcadeConfig::COLOR_YELLOW);
        canvas.setCursor(4, 1);
        canvas.print("DIST:"); canvas.print(_score);

        canvas.setTextColor(ArcadeConfig::COLOR_GREY);
        canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH - 60, 1);
        canvas.print("HI:"); canvas.print(_highScore);
    }

    void startNewGame(AudioEngine &audio) {
        _score = 0;
        _lastEnemyUnlockTier = 0;
        _particles.clearAll();
        _platforms.initGame();
        _enemies.initGame();
        _powerUp.reset();
        _playerXOffset = 0.0f;
        _player.reset((float)ArcadeConfig::RUNNER_BASE_X, ArcadeConfig::LANDSCAPE_HEIGHT - 8);
        _uiDirty = true;
        _phase = PHASE_PLAYING;
        _phaseTimer = millis();
    }

    void triggerPlayerDeath(AudioEngine &audio) {
        _particles.triggerExplosion(_player.getX() + RUNNER_WIDTH / 2.0f,
                                     _player.getY() + RUNNER_HEIGHT / 2.0f);
        audio.playSound(200, 250);
    }

public:
    PlatformFluxGame() {}

    void init(AudioEngine &audio) override {
        loadHighScore();
        _phase       = PHASE_ATTRACT;
        _btnBWasHeld = true;
    }

    void setTFT(Adafruit_ST7735 &tft) { _tft = &tft; }

    bool update(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) override {
        // Button B: require release first, then hold 2s to exit
        static unsigned long btnBHoldStart = 0;
        if (_btnBWasHeld) {
            if (!input.btnB) _btnBWasHeld = false;
        } else if (input.btnB) {
            if (btnBHoldStart == 0) btnBHoldStart = millis();
            if (millis() - btnBHoldStart > 2000) {
                btnBHoldStart = 0;
                audio.mute();
                return false;
            }
        } else {
            btnBHoldStart = 0;
        }

        // ---- PHASE: ATTRACT ----
        if (_phase == PHASE_ATTRACT) {
            canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
            canvas.setTextSize(2);
            canvas.setTextColor(ArcadeConfig::COLOR_GREEN);
            canvas.setCursor(14, 30);
            canvas.print("PLATFORM");
            canvas.setCursor(40, 50);
            canvas.print("FLUX");

            canvas.setTextSize(1);
            canvas.setTextColor(ArcadeConfig::COLOR_GREY);
            canvas.setCursor(20, 80);
            canvas.print("BEST DIST: "); canvas.print(_highScore);
            canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
            canvas.setCursor(18, 100);
            canvas.print("[BTN A] TO START");
            flushLandscape(canvas);

            if (input.btnBPressed) { audio.mute(); return false; }
            if (input.btnAPressed) startNewGame(audio);
            return true;
        }

        // ---- PHASE: PLAYING ----
        if (_phase == PHASE_PLAYING) {
            bool uiNeedsUpdate = false;
            bool playerHit     = false;

            _particles.update();
            _platforms.update();
            _platforms.advanceDifficulty();

            // Flying enemies stay off entirely at first — the first one
            // unlocks at tier 2, the second at tier 4, so hazards ramp in
            // alongside the platform gaps/speed rather than from frame one.
            int tier = _platforms.getTier();
            if (tier >= 2 && _lastEnemyUnlockTier < 1) {
                _enemies.unlockNextEnemy();
                _lastEnemyUnlockTier = 1;
            }
            if (tier >= 4 && _lastEnemyUnlockTier < 2) {
                _enemies.unlockNextEnemy();
                _lastEnemyUnlockTier = 2;
            }

            if (input.btnAPressed) _player.jump();

            // Joystick nudges the runner forward/back within a bounded range —
            // rotation-1 games read joyY for on-screen horizontal, same swap
            // AsteroidFlux uses for its physical orientation.
            _playerXOffset += input.joyY * ArcadeConfig::RUNNER_X_MOVE_SPEED;
            _playerXOffset  = constrain(_playerXOffset,
                                        (float)ArcadeConfig::RUNNER_X_MIN_OFFSET,
                                        (float)ArcadeConfig::RUNNER_X_MAX_OFFSET);
            _player.setX((float)ArcadeConfig::RUNNER_BASE_X + _playerXOffset);

            float px = _player.getX(), pRight = px + RUNNER_WIDTH;
            float py = _player.getY(), pBottom = py + RUNNER_HEIGHT;
            int ground = _platforms.groundYAt(px, pRight, py, pBottom);
            float groundTarget = (ground == -1) ? (float)(ArcadeConfig::LANDSCAPE_HEIGHT + 40) : (float)ground;

            _player.updatePhysics(groundTarget);
            _player.updateAnimation();
            _player.updateInvincibility();

            _powerUp.maybeSpawn(ArcadeConfig::LANDSCAPE_WIDTH, ArcadeConfig::LANDSCAPE_HEIGHT - 8);
            _powerUp.update(_platforms.getScrollSpeed(), _player, _particles, audio, uiNeedsUpdate);
            _enemies.update(_platforms.getScrollSpeed(), _player, _particles, audio, playerHit);

            // Falling off the bottom of the screen is also a death condition.
            if (_player.getY() > ArcadeConfig::LANDSCAPE_HEIGHT + 20) playerHit = true;

            // Score ticks with distance travelled (matches PlatformManager's
            // internal distance counter so difficulty and score stay in sync).
            static unsigned long lastScoreTick = 0;
            if (millis() - lastScoreTick > 100) {
                _score++;
                lastScoreTick = millis();
                uiNeedsUpdate = true;
            }

            if (playerHit && !_player.isInvincible()) {
                triggerPlayerDeath(audio);
                if (_score > _highScore) { _highScore = _score; saveHighScore(); }
                _phase = PHASE_DEATH;
                _phaseTimer = millis();

                canvas.fillRect(0, 11, ArcadeConfig::LANDSCAPE_WIDTH,
                                ArcadeConfig::LANDSCAPE_HEIGHT - 11, ArcadeConfig::COLOR_BLACK);
                _particles.render(canvas, 11);
                drawUI(canvas);
                flushLandscape(canvas);
                return true;
            }

            canvas.fillRect(0, 11, ArcadeConfig::LANDSCAPE_WIDTH,
                            ArcadeConfig::LANDSCAPE_HEIGHT - 11, ArcadeConfig::COLOR_BLACK);
            _platforms.render(canvas);
            _particles.render(canvas, 11);
            _powerUp.render(canvas);
            _enemies.render(canvas);
            _player.render(canvas);

            if (uiNeedsUpdate || _uiDirty) {
                drawUI(canvas);
                _uiDirty = false;
            }

            flushLandscape(canvas);
            return true;
        }

        // ---- PHASE: DEATH — let the disintegration play out ----
        if (_phase == PHASE_DEATH) {
            _particles.update();
            canvas.fillRect(0, 11, ArcadeConfig::LANDSCAPE_WIDTH,
                            ArcadeConfig::LANDSCAPE_HEIGHT - 11, ArcadeConfig::COLOR_BLACK);
            _particles.render(canvas, 11);
            drawUI(canvas);
            flushLandscape(canvas);

            if (millis() - _phaseTimer > 800) {
                _particles.clearAll();
                _phase             = PHASE_GAMEOVER;
                _gameOverEnteredMs = millis();
            }
            return true;
        }

        // ---- PHASE: GAME OVER ----
        if (_phase == PHASE_GAMEOVER) {
            canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
            canvas.setTextColor(ArcadeConfig::COLOR_RED);
            canvas.setTextSize(2);
            canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH / 4 - 12, 15);
            canvas.print("GAME OVER");

            canvas.setTextSize(1);
            canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
            canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH / 4, 45);
            canvas.print("DIST: "); canvas.print(_score);

            if (_score >= _highScore && _score > 0) {
                canvas.setTextColor(ArcadeConfig::COLOR_GREEN);
                canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH / 4, 65);
                canvas.print("NEW HIGH SCORE!!");
            } else {
                canvas.setTextColor(ArcadeConfig::COLOR_GREY);
                canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH / 4, 65);
                canvas.print("BEST: "); canvas.print(_highScore);
            }

            canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
            canvas.setCursor(20, 90);
            canvas.print("[BTN A] PLAY AGAIN");
            canvas.setCursor(20, 103);
            canvas.print("[BTN B] QUIT");

            unsigned long elapsed = millis() - _gameOverEnteredMs;
            if (elapsed > (GAMEOVER_TIMEOUT_MS - 10000UL)) {
                int secsLeft = (int)((GAMEOVER_TIMEOUT_MS - elapsed) / 1000UL) + 1;
                canvas.setTextColor(ArcadeConfig::COLOR_AMBER);
                canvas.setCursor(20, 116);
                canvas.print("AUTO: "); canvas.print(secsLeft); canvas.print("s");
            }

            flushLandscape(canvas);

            if (input.btnAPressed) { startNewGame(audio); return true; }
            if (input.btnBPressed) { audio.mute(); return false; }

            if (elapsed > GAMEOVER_TIMEOUT_MS) {
                _phase       = PHASE_ATTRACT;
                _btnBWasHeld = false;
            }
            return true;
        }

        return true;
    }

    uint8_t getRotation() const override { return 1; }
    const char* getName()  const override { return "Platform Flux"; }

private:
    void flushLandscape(GFXcanvas16 &canvas) {
        if (_tft) {
            _tft->drawRGBBitmap(0, 0, canvas.getBuffer(),
                                ArcadeConfig::LANDSCAPE_WIDTH,
                                ArcadeConfig::LANDSCAPE_HEIGHT);
        }
    }
};

#endif // PLATFORM_FLUX_GAME_H
