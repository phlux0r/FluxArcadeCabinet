#ifndef ASTEROID_FLUX_GAME_H
#define ASTEROID_FLUX_GAME_H

#include "../../games/IGame.h"
#include "../../cabinet/ArcadeConfig.h"
#include "../../cabinet/ParticleManager.h"
#include "../../cabinet/AudioEngine.h"

#include "AsteroidManager.h"
#include "PlayerShip.h"
#include "PowerUpManager.h"
#include "BackgroundStars.h"
#include "NebulaManager.h"

// Game-specific bitmap assets (splash is Asteroid Flux only)
#include "assets/splash_image.h"

// Shared audio assets (also used by Lander Flux)
#include "../../assets/shared/SharedAssets.h"

#include <Preferences.h>
#include <Fonts/TomThumb.h>

class AsteroidFluxGame : public IGame {
private:
    PlayerShip      _ship;
    PowerUpManager  _powerUps;
    AsteroidManager _asteroids;
    BackgroundStars _background;
    NebulaManager   _nebula;
    ParticleManager _particles;

    Preferences      _prefs;
    Adafruit_ST7735* _tft = nullptr;

    int  _score           = 0;
    int  _highScore       = 0;
    int  _lives           = 3;
    int  _asteroidsPassed = 0;
    int  _nextTargetScore = 0;

    enum GamePhase { PHASE_ATTRACT, PHASE_COUNTDOWN, PHASE_PLAYING, PHASE_HIT, PHASE_GAMEOVER };
    GamePhase _phase = PHASE_ATTRACT;

    enum AttractSlide { SLIDE_SPLASH, SLIDE_INFO };
    AttractSlide  _attractSlide      = SLIDE_SPLASH;
    unsigned long _attractSlideTimer  = 0;

    unsigned long _phaseTimer   = 0;
    int           _countdownVal = 3;
    bool          _uiDirty      = true;

    // Guards against instant exit on launch
    bool _btnBWasHeld = false;

    // Game-over 30-second attract timeout
    unsigned long _gameOverEnteredMs = 0;
    static const unsigned long GAMEOVER_TIMEOUT_MS = 30000UL;

    // Ship movement — both axes velocity-based at same speed
    float _shipXOffset = 0.0f;
    float _shipYOffset = 0.0f;
    static const int   SHIP_X_MIN = 15;
    static const int   SHIP_X_MAX = ArcadeConfig::LANDSCAPE_WIDTH / 3;
    static const int   SHIP_Y_MIN = ArcadeConfig::UI_MARGIN_TOP + 1;
    static const int   SHIP_Y_MAX = ArcadeConfig::LANDSCAPE_HEIGHT - ArcadeConfig::SHIP_HEIGHT - 1;
    static constexpr float SHIP_MOVE_SPEED = 1.2f;  // px/frame, same for both axes

    // Countdown WAV timing — set from WAV duration header on first play
    uint32_t _countdownDurationMs = 1800;  // fallback if no SD card
    bool     _countdownWAVReady   = false;

    // Attract music — track whether loop command has been issued
    // (independent of audio task playing state to avoid restart loop)
    bool _attractMusicStarted = false;

    // Last-life hit — route through PHASE_HIT before PHASE_GAMEOVER
    bool _gameOverPending = false;

    void loadHighScore() {
        _prefs.begin("af_data", true);
        _highScore = _prefs.getInt("highscore", 0);
        _prefs.end();
    }

    void saveHighScore() {
        _prefs.begin("af_data", false);
        _prefs.putInt("highscore", _highScore);
        _prefs.end();
    }

    void drawUI(GFXcanvas16 &canvas) {
        canvas.fillRect(0, 0, ArcadeConfig::LANDSCAPE_WIDTH, 10, ArcadeConfig::COLOR_BLACK);
        canvas.drawFastHLine(0, 10, ArcadeConfig::LANDSCAPE_WIDTH, ArcadeConfig::COLOR_GREEN);

        canvas.setTextSize(1);
        canvas.setTextColor(ArcadeConfig::COLOR_YELLOW);
        canvas.setCursor(4, 1);
        canvas.print("SCORE:"); canvas.print(_score);

        canvas.setTextColor(ArcadeConfig::COLOR_GREY);
        canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH / 2 - 10, 1);
        canvas.print("HI:"); canvas.print(_highScore);

        canvas.setTextColor(ArcadeConfig::COLOR_RED);
        canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH - 40, 1);
        canvas.print(_lives); canvas.print(" UP");
    }

    void renderSplash(GFXcanvas16 &canvas) {
        for (int i = 0; i < 20480; i++) {
            uint8_t b1 = splash_bitmap[i * 2];
            uint8_t b2 = splash_bitmap[i * 2 + 1];
            uint16_t px = (b2 << 8) | b1;
            canvas.drawPixel(i % ArcadeConfig::LANDSCAPE_WIDTH,
                             i / ArcadeConfig::LANDSCAPE_WIDTH, px);
        }
        canvas.setTextSize(1);
        canvas.setTextColor(ArcadeConfig::COLOR_GREY);
        canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH / 4 - 4, 94);
        canvas.print("BEST: "); canvas.print(_highScore);
        canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
        canvas.setCursor(18, 115);
        canvas.print("[BTN A] TO START");
    }

    void renderInfoScreen(GFXcanvas16 &canvas) {
        canvas.fillScreen(ArcadeConfig::COLOR_BLACK);

        // --- REWARDS section ---
        // Header: textSize 1 = 6px/char. "---== REWARDS ==---" = 19 chars = 114px → x=23
        canvas.setFont();
        canvas.setTextSize(1);
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.setCursor(23, 4);
        canvas.print("---== REWARDS ==---");

        // TomThumb rows: 5px/char + 1px gap = 6px/char
        // "YELLOW(S1)  CYAN(S2)" = 20 chars = 120px → x=20
        // Row spacing: 13px for comfortable reading
        canvas.setFont(&TomThumb);
        canvas.setTextColor(ArcadeConfig::COLOR_YELLOW);
        canvas.setCursor(20, 18); canvas.print("YELLOW +1    CYAN +2");
        canvas.setTextColor(ArcadeConfig::COLOR_ORANGE);
        canvas.setCursor(20, 31); canvas.print("ORANGE +3     RED +4");
        canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
        canvas.setCursor(20, 44); canvas.print("BLUE +5    COMET +15");

        // --- POWERUPS section ---
        // Header: "---== POWERUPS ==---" = 20 chars = 120px → x=20
        canvas.setFont();
        canvas.setTextSize(1);
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.setCursor(20, 60);
        canvas.print("---== POWERUPS ==---");

        // TomThumb rows — "SHIELD:  ABSORBS 1 HIT" = 22 chars = 132px → x=14
        canvas.setFont(&TomThumb);
        canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
        canvas.setCursor(14, 76);  canvas.print("CLOCK:   SLOWS SECTOR");
        canvas.setTextColor(ArcadeConfig::COLOR_GREEN);
        canvas.setCursor(14, 89);  canvas.print("SHIELD:  ABSORBS 1 HIT");
        canvas.setTextColor(ArcadeConfig::COLOR_RED);
        canvas.setCursor(14, 102); canvas.print("HEART:   EXTRA LIFE");

        // Footer
        canvas.setFont();
        canvas.setTextColor(0x5AEB);
        canvas.setCursor(24, 118);
        canvas.print("[BTN A] TO START");
    }

    void startNewGame(AudioEngine &audio) {
        _score           = 0;
        _lives           = 3;
        _asteroidsPassed = 0;
        _nextTargetScore = ArcadeConfig::SCORE_TO_SPAWN;
        _ship.reset();
        _powerUps.resetTimeline();
        _particles.clearAll();
        _asteroids.initGame();
        _shipXOffset  = 0.0f;
        _shipYOffset  = (float)(ArcadeConfig::LANDSCAPE_HEIGHT / 2);
        _uiDirty      = true;
        _phase        = PHASE_COUNTDOWN;
        _countdownVal = 3;
        _phaseTimer   = millis();
        _gameOverPending     = false;
        _attractMusicStarted = false;

        // Stop attract music, play countdown WAV
        audio.playWAV("/audio/countdown.wav");
        _countdownWAVReady = false;
    }

    // Spawn explosion particles and start non-blocking WAV stream.
    // Graphics continue normally — audio feeds through update() each frame.
    void triggerShipExplosion(AudioEngine &audio) {
        float cx = _ship.getX() + 8.0f;
        float cy = (float)_ship.getY() + 5.0f;
        _particles.spawnExplosion(cx, cy, ArcadeConfig::COLOR_ION_BLUE, 20);
        _particles.spawnExplosion(cx, cy, ArcadeConfig::COLOR_YELLOW, 10);
        // Non-blocking: audio streams in chunks via audio.update() each frame
        audio.playExplosionSound(explosion_data, sizeof(explosion_data));
    }

public:
    AsteroidFluxGame() {}

    void init(AudioEngine &audio) override {
        loadHighScore();
        _phase             = PHASE_ATTRACT;
        _attractSlide      = SLIDE_SPLASH;
        _attractSlideTimer = millis();
        _btnBWasHeld       = true;
        _shipXOffset       = 0.0f;
        _shipYOffset       = (float)(ArcadeConfig::LANDSCAPE_HEIGHT / 2);
        _countdownWAVReady = false;
        _attractMusicStarted = false;
        _gameOverPending   = false;
        audio.playStartupSound(gamestart_data, sizeof(gamestart_data));
    }

    void setTFT(Adafruit_ST7735 &tft) { _tft = &tft; }

    bool update(GFXcanvas16 &canvas,
                const InputState &input,
                AudioEngine &audio) override {

        // --- Button B: require release first, then hold 2s to exit ---
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
            // Issue loop command once only — not every frame
            if (!_attractMusicStarted) {
                Serial.printf("[Asteroid] Playing loop");
                audio.loopWAV("/audio/asteroid_loop.wav");
                _attractMusicStarted = true;
            }

            if (millis() - _attractSlideTimer > 8000) {
                _attractSlide      = (_attractSlide == SLIDE_SPLASH) ? SLIDE_INFO : SLIDE_SPLASH;
                _attractSlideTimer = millis();
            }

            if (_attractSlide == SLIDE_SPLASH) renderSplash(canvas);
            else                                renderInfoScreen(canvas);

            if (input.btnBPressed) {
                audio.mute();
                return false;
            }

            if (input.btnAPressed) {
                audio.stopLoop();
                startNewGame(audio);
            }

            flushLandscape(canvas);
            return true;
        }

        // ---- PHASE: COUNTDOWN ----
        if (_phase == PHASE_COUNTDOWN) {
            canvas.fillRect(0, 11,
                            ArcadeConfig::LANDSCAPE_WIDTH,
                            ArcadeConfig::LANDSCAPE_HEIGHT - 11,
                            ArcadeConfig::COLOR_BLACK);
            drawUI(canvas);

            // Read WAV duration once it's available from the audio task.
            // Reset phaseTimer at that moment so countdown starts from zero
            // relative to the actual WAV length (not the fallback 1800ms).
            if (!_countdownWAVReady && audio.getLastWAVDurationMs() > 0) {
                _countdownDurationMs = audio.getLastWAVDurationMs();
                _countdownWAVReady   = true;
                // Compensate for I2S DMA pipeline latency (~40ms from write to speaker)
                // and file open/header parse time to align visuals with audio
                _phaseTimer = millis() - 40;
            }

            unsigned long elapsed = millis() - _phaseTimer;

            // Show a large countdown number that scales with WAV duration
            // Divide total duration into thirds for 3→2→1
            int third = _countdownDurationMs / 3;
            int val = (elapsed < third) ? 3 : (elapsed < third * 2) ? 2 : 1;

            if (elapsed < _countdownDurationMs) {
                canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH / 2 - 6,
                                 ArcadeConfig::LANDSCAPE_HEIGHT / 2 - 8);
                canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
                canvas.setTextSize(2);
                canvas.print(val);
                canvas.setTextSize(1);
            } else {
                _phase = PHASE_PLAYING;
            }
            flushLandscape(canvas);
            return true;
        }

        // ---- PHASE: PLAYING ----
        if (_phase == PHASE_PLAYING) {
            bool uiNeedsUpdate = false;
            bool playerHit     = false;

            _nebula.update();
            _background.update();
            _particles.update();

            _ship.updatePosition(input.joyY);  // no-op, kept for compat

            // Y-axis: velocity-based, negated to match physical joystick direction
            _shipYOffset += -input.joyY * SHIP_MOVE_SPEED;
            _shipYOffset  = constrain(_shipYOffset,
                                      (float)SHIP_Y_MIN,
                                      (float)SHIP_Y_MAX);
            _ship.setY((int)_shipYOffset);

            // X-axis: joystick X lets ship push into field up to 1/3 screen width
            _shipXOffset += input.joyX * SHIP_MOVE_SPEED;
            _shipXOffset  = constrain(_shipXOffset, 0.0f,
                                      (float)(SHIP_X_MAX - SHIP_X_MIN));
            _ship.setX((float)SHIP_X_MIN + _shipXOffset);

            _ship.updateAnimation();
            _ship.updateShield();

            _powerUps.update(_score, _ship, _lives, uiNeedsUpdate, audio, _asteroids);
            _asteroids.update(_ship, _score, _asteroidsPassed, _nextTargetScore,
                              uiNeedsUpdate, playerHit, audio, _particles);

            if (playerHit) {
                triggerShipExplosion(audio);
                _lives--;

                // Always go through PHASE_HIT first so explosion plays out.
                // _gameOverPending signals that PHASE_HIT should transition to
                // PHASE_GAMEOVER instead of PHASE_COUNTDOWN.
                _phase      = PHASE_HIT;
                _phaseTimer = millis();
                _asteroids.forceBoardWipe();

                if (_lives <= 0) {
                    if (_score > _highScore) { _highScore = _score; saveHighScore(); }
                    _gameOverPending = true;
                    // Game-over sound plays after explosion settles in PHASE_HIT
                } else {
                    _gameOverPending = false;
                }

                // Render one explosion frame before phase switch
                canvas.fillRect(0, 11,
                                ArcadeConfig::LANDSCAPE_WIDTH,
                                ArcadeConfig::LANDSCAPE_HEIGHT - 11,
                                ArcadeConfig::COLOR_BLACK);
                _particles.render(canvas, 11);
                drawUI(canvas);
                flushLandscape(canvas);
                return true;
            }

            // Normal gameplay render
            canvas.fillRect(0, 11,
                            ArcadeConfig::LANDSCAPE_WIDTH,
                            ArcadeConfig::LANDSCAPE_HEIGHT - 11,
                            ArcadeConfig::COLOR_BLACK);
            _nebula.render(canvas);
            _background.render(canvas);
            _particles.render(canvas, 11);
            _powerUps.render(canvas);
            _asteroids.render(canvas);
            _ship.render(canvas);

            if (uiNeedsUpdate || _uiDirty) {
                drawUI(canvas);
                _uiDirty = false;
            }

            flushLandscape(canvas);
            return true;
        }

        // ---- PHASE: HIT — show explosion, then countdown or game over ----
        if (_phase == PHASE_HIT) {
            _particles.update();
            canvas.fillRect(0, 11,
                            ArcadeConfig::LANDSCAPE_WIDTH,
                            ArcadeConfig::LANDSCAPE_HEIGHT - 11,
                            ArcadeConfig::COLOR_BLACK);
            _particles.render(canvas, 11);
            drawUI(canvas);
            flushLandscape(canvas);

            if (millis() - _phaseTimer > 800) {
                _particles.clearAll();
                if (_gameOverPending) {
                    // Explosion done — now play game-over sound and show screen
                    _phase             = PHASE_GAMEOVER;
                    _phaseTimer        = millis();
                    _gameOverEnteredMs = millis();
                    _gameOverPending   = false;
                    audio.playGameOverSound(gameend_data, sizeof(gameend_data));
                } else {
                    // Respawn countdown
                    _phase        = PHASE_COUNTDOWN;
                    _countdownVal = 3;
                    _phaseTimer   = millis();
                    audio.playWAV("/audio/countdown.wav");
                    _countdownWAVReady = false;
                }
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
            canvas.print("SCORE: "); canvas.print(_score);

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

            // Show countdown in last 10 seconds before attract timeout
            unsigned long elapsed = millis() - _gameOverEnteredMs;
            if (elapsed > (GAMEOVER_TIMEOUT_MS - 10000UL)) {
                int secsLeft = (int)((GAMEOVER_TIMEOUT_MS - elapsed) / 1000UL) + 1;
                canvas.setTextColor(ArcadeConfig::COLOR_AMBER);
                canvas.setCursor(20, 116);
                canvas.print("AUTO: "); canvas.print(secsLeft); canvas.print("s");
            }

            flushLandscape(canvas);

            // Button A: play again
            if (input.btnAPressed) {
                startNewGame(audio);
                return true;
            }

            // Button B: exit to launcher immediately (no hold needed from game over)
            if (input.btnBPressed) {
                audio.mute();
                return false;
            }

            // 30-second timeout: return to attract mode
            if (elapsed > GAMEOVER_TIMEOUT_MS) {
                _phase               = PHASE_ATTRACT;
                _attractSlide        = SLIDE_SPLASH;
                _attractSlideTimer   = millis();
                _btnBWasHeld         = false;
                _attractMusicStarted = false;  // Allow music to restart
            }

            return true;
        }

        return true;
    }

    uint8_t getRotation() const override { return 1; }
    const char* getName()  const override { return "Asteroid Flux"; }

private:
    void flushLandscape(GFXcanvas16 &canvas) {
        if (_tft) {
            _tft->drawRGBBitmap(0, 0, canvas.getBuffer(),
                                ArcadeConfig::LANDSCAPE_WIDTH,
                                ArcadeConfig::LANDSCAPE_HEIGHT);
        }
    }
};

#endif // ASTEROID_FLUX_GAME_H