#ifndef ASTEROID_FLUX_GAME_H
#define ASTEROID_FLUX_GAME_H

#include "../../games/IGame.h"
#include "../../cabinet/ArcadeConfig.h"
#include "../../cabinet/ParticleManager.h"
#include "../../cabinet/AudioEngine.h"
#include "../../cabinet/HighScores.h"

#include "AsteroidManager.h"
#include "PlayerShip.h"
#include "PowerUpManager.h"
#include "BoltManager.h"
#include "AlienSaucer.h"
#include "SpaceBackdrop.h"

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
    SpaceBackdrop   _backdrop;
    uint32_t        _backdropSeed = 0;   // a fresh sky each game and demo
    ParticleManager _particles;
    BoltManager     _bolts;
    AlienSaucer     _saucer;
    bool            _fireWasOn = false;      // a Fire pickup may bring the saucer

    Adafruit_ST7735* _tft = nullptr;

    // The Fire power-up: when A may next shoot, whether the timer bar was
    // up last frame (the HUD line goes back to green when it ends), and
    // which of its sounds are on the card (checked once, in init()).
    unsigned long _nextShotMs = 0;
    bool _fireBarShown = false;
    bool _shotOnCard = false, _hitOnCard = false, _ufoOnCard = false;
    static constexpr const char* SHOT_WAV = "/audio/tube_shot.wav";
    static constexpr const char* HIT_WAV  = "/audio/shot.wav";
    static constexpr const char* UFO_WAV  = "/audio/asteroid_ufo.wav";

    int  _score           = 0;
    int  _highScore       = 0;
    int  _lives           = 3;
    int  _asteroidsPassed = 0;
    int  _nextTargetScore = 0;

    // NAME: entering a name for the high-score table, after the last life.
    enum GamePhase { PHASE_ATTRACT, PHASE_PLAYING, PHASE_HIT, PHASE_NAME, PHASE_GAMEOVER };
    GamePhase _phase = PHASE_ATTRACT;

    // Title, how-to-play, then the autopilot demo (a game with _demo set).
    enum AttractSlide { SLIDE_SPLASH, SLIDE_INFO, SLIDE_SCORES };
    bool _demo = false;
    unsigned long _demoUntil = 0;
    float _demoTargetX = 0.0f, _demoTargetY = 0.0f;
    static const int DEMO_HORIZON = 45;        // frames the autopilot predicts
    static const unsigned long DEMO_MIN_MS = 30000, DEMO_MAX_MS = 40000;
    float _demoXs[DEMO_HORIZON * ArcadeConfig::MAX_ASTEROIDS];
    float _demoYs[DEMO_HORIZON * ArcadeConfig::MAX_ASTEROIDS];
    AttractSlide  _attractSlide      = SLIDE_SPLASH;
    unsigned long _attractSlideTimer  = 0;

    unsigned long _phaseTimer   = 0;
    bool          _uiDirty      = true;

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

    // A game starts, and a lost life respawns, straight into play: the ship
    // gets this long of flashing shield instead of a countdown. (A hit also
    // clears the board, so a respawn doesn't land among asteroids.)
    static const unsigned long SPAWN_SHIELD_MS = 2000;

    // Last-life hit — route through PHASE_HIT before PHASE_GAMEOVER
    bool _gameOverPending = false;

    // The cabinet's table for this game; _highScore is its top score.
    hiscore::ScoreBoard _scores;

    void loadHighScore() {
        _scores.begin("asteroids");
        _highScore = (int)_scores.best();
    }

    void renderScoresScreen(GFXcanvas16 &canvas) {
        canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
        hiscore::drawTable(canvas, _scores.table(), "HIGH SCORES", 18);
        if ((millis() / 500) & 1) hiscore::printCentred(canvas, "[BTN A] TO START", 108, ArcadeConfig::COLOR_CYAN);
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

    // The Fire power-up's timer: the line under the HUD, orange from the
    // left for the time left and green beyond, flashing in its last 3s.
    void drawFireBar(GFXcanvas16 &canvas) {
        const int W = ArcadeConfig::LANDSCAPE_WIDTH;
        const unsigned long left = _ship.fireRemainingMs();
        int w = (int)((unsigned long)W * left / ArcadeConfig::FIRE_DURATION_MS);
        if (left < 3000 && (millis() / 125) % 2) w = 0;
        canvas.drawFastHLine(0, 10, W, ArcadeConfig::COLOR_GREEN);
        if (w > 0) canvas.drawFastHLine(0, 10, w, ArcadeConfig::COLOR_ORANGE);
    }

    void sfxShot(AudioEngine &audio) {
        if (_shotOnCard) audio.playWAV(SHOT_WAV);
        else             audio.playSound(1800, 25);
    }
    void sfxHit(AudioEngine &audio) {
        if (_hitOnCard) audio.playWAV(HIT_WAV);
        else            audio.playSound(500, 60);
    }
    // The saucer arriving: its own WAV, else a rising and falling warble.
    void sfxSaucer(AudioEngine &audio) {
        if (_ufoOnCard) { audio.playWAV(UFO_WAV); return; }
        static const int f[] = { 600, 900, 1200, 900, 600, 900, 1200, 900 };
        static const int d[] = { 60, 60, 60, 60, 60, 60, 60, 60 };
        audio.playMelody(f, d, 8);
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
        char hiBuf[24];
        canvas.print(_scores.bestLine(hiBuf, sizeof(hiBuf), "BEST: "));
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
        // Row spacing: 10px, four rows above the power-ups
        canvas.setFont(&TomThumb);
        canvas.setTextColor(ArcadeConfig::COLOR_YELLOW);
        canvas.setCursor(20, 17); canvas.print("YELLOW +1    CYAN +2");
        canvas.setTextColor(ArcadeConfig::COLOR_ORANGE);
        canvas.setCursor(20, 27); canvas.print("ORANGE +3     RED +4");
        canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
        canvas.setCursor(20, 37); canvas.print("BLUE +5    COMET +15");
        canvas.setTextColor(ArcadeConfig::COLOR_MAGENTA);
        canvas.setCursor(20, 47); canvas.print("SAUCER, 3 HITS +1000");

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
        canvas.setCursor(14, 75);  canvas.print("CLOCK:   SLOWS SECTOR");
        canvas.setTextColor(ArcadeConfig::COLOR_GREEN);
        canvas.setCursor(14, 85);  canvas.print("SHIELD:  ABSORBS 1 HIT");
        canvas.setTextColor(ArcadeConfig::COLOR_RED);
        canvas.setCursor(14, 95);  canvas.print("HEART:   EXTRA LIFE");
        canvas.setTextColor(ArcadeConfig::COLOR_ORANGE);
        canvas.setCursor(14, 105); canvas.print("FIRE:    HOLD A, 20 SEC");

        // Footer
        canvas.setFont();
        canvas.setTextColor(0x5AEB);
        canvas.setCursor(24, 118);
        canvas.print("[BTN A] TO START");
    }

    void startNewGame(AudioEngine &audio) {
        resetGame();
        // Music plays during a game only: not on the attract screen, kept
        // through respawns, stopped at game over.
        audio.loopWAV("/audio/flux-asteroids.wav");
    }

    void resetGame() {
        _score           = 0;
        _lives           = 3;
        _asteroidsPassed = 0;
        _nextTargetScore = ArcadeConfig::SCORE_TO_SPAWN;
        _ship.reset();
        _powerUps.newGame();
        _particles.clearAll();
        _bolts.clearAll();
        _saucer.reset();
        _fireWasOn    = false;
        _fireBarShown = false;
        _asteroids.initGame();
        _backdrop.reset(++_backdropSeed);
        _shipXOffset  = 0.0f;
        _shipYOffset  = (float)(ArcadeConfig::LANDSCAPE_HEIGHT / 2);
        _uiDirty      = true;
        _phase        = PHASE_PLAYING;
        _phaseTimer   = millis();
        _gameOverPending = false;
        _ship.activateShield(SPAWN_SHIELD_MS);
    }

    // ---- Attract demo ---------------------------------------------------------

    // How close the ship would come to any asteroid over the next
    // DEMO_HORIZON frames if it steered from here to (tx, ty) at full stick:
    // the smallest gap between the ship's box and an asteroid's hit circle.
    float demoClearance(float tx, float ty, const float* rs) const {
        const int N = ArcadeConfig::MAX_ASTEROIDS;
        float xo = _shipXOffset, yo = _shipYOffset, worst = 1e9f;
        for (int t = 0; t < DEMO_HORIZON; t++) {
            xo += constrain((tx - xo) / SHIP_MOVE_SPEED, -1.0f, 1.0f) * SHIP_MOVE_SPEED;
            yo += constrain((ty - yo) / SHIP_MOVE_SPEED, -1.0f, 1.0f) * SHIP_MOVE_SPEED;
            const float sx = (float)SHIP_X_MIN + xo, sy = (float)(int)yo;
            for (int i = 0; i < N; i++) {
                if (rs[i] <= 0.0f) continue;
                const float ax = _demoXs[t * N + i], ay = _demoYs[t * N + i];
                const float cx = max(sx, min(ax, sx + ArcadeConfig::SHIP_WIDTH));
                const float cy = max(sy, min(ay, sy + ArcadeConfig::SHIP_HEIGHT));
                const float gap = sqrtf((ax - cx) * (ax - cx) + (ay - cy) * (ay - cy)) - rs[i];
                if (gap < worst) worst = gap;
            }
            // The saucer's plasma balls, flying straight on.
            for (int k = 0; k < AlienSaucer::MAX_SHOTS; k++) {
                const AlienSaucer::Shot &p = _saucer.shots()[k];
                if (!p.active) continue;
                const float ax = p.x + p.vx * (t + 1), ay = p.y + p.vy * (t + 1);
                const float cx = max(sx, min(ax, sx + ArcadeConfig::SHIP_WIDTH));
                const float cy = max(sy, min(ay, sy + ArcadeConfig::SHIP_HEIGHT));
                const float gap = sqrtf((ax - cx) * (ax - cx) + (ay - cy) * (ay - cy)) - 2.5f;
                if (gap < worst) worst = gap;
            }
        }
        return worst;
    }

    // Steers for whichever spot keeps the ship furthest from every asteroid
    // over the next DEMO_HORIZON frames, sticking with its current aim unless
    // another is clearly better (so it doesn't twitch).
    InputState demoPilot() {
        InputState in{};
        float rs[ArcadeConfig::MAX_ASTEROIDS];
        _asteroids.predictPaths(DEMO_HORIZON, _demoXs, _demoYs, rs);
        const float xSpan = (float)(SHIP_X_MAX - SHIP_X_MIN);
        // With the saucer about, any spot clear enough will do, and the
        // one most in line with it (to shoot it) is best.
        auto worth = [&](float tx, float ty) {
            const float c = demoClearance(tx, ty, rs);
            if (!_saucer.onScreen()) return c;
            return min(c, 10.0f) - 0.08f * fabsf(ty + 4.0f - _saucer.y());   // bolts leave at the ship's y + 4
        };
        float bestX = _demoTargetX, bestY = _demoTargetY;
        float best = worth(bestX, bestY) + 3.0f;   // the current aim's head start
        for (float ty = (float)SHIP_Y_MIN; ty <= (float)SHIP_Y_MAX; ty += 6.0f) {
            for (int k = 0; k < 3; k++) {
                const float tx = xSpan * 0.5f * (float)k;
                float c = worth(tx, ty);
                if (c > best) { best = c; bestX = tx; bestY = ty; }
            }
        }
        _demoTargetX = bestX;
        _demoTargetY = bestY;
        in.btnA = _ship.isFireActive();          // shoots whenever it can
        // Rotation-1 axes, as updatePlaying's input reads them.
        in.joyX = constrain((bestY - _shipYOffset) / SHIP_MOVE_SPEED, -1.0f, 1.0f);
        in.joyY = constrain((bestX - _shipXOffset) / SHIP_MOVE_SPEED, -1.0f, 1.0f);
        return in;
    }

    // A game some way in: a random 3-6 asteroids at a matching speed.
    void startDemo() {
        resetGame();
        _demo = true;
        const int active = (int)random(3, ArcadeConfig::MAX_ASTEROIDS + 1);
        _asteroids.setDemoField(active, ArcadeConfig::BASE_SPEED + ArcadeConfig::SPEED_STEP * (float)random(0, 8));
        _nextTargetScore = 1000000;               // the field stays as it is
        _demoTargetX = _shipXOffset;
        _demoTargetY = _shipYOffset;
        _demoUntil = millis() + (unsigned long)random((long)DEMO_MIN_MS, (long)DEMO_MAX_MS + 1);
    }

    // Back to the title, leaving nothing of the demo behind.
    void endDemo() {
        _demo = false;
        resetGame();
        _score = 0;
        _phase = PHASE_ATTRACT;
        _attractSlide = SLIDE_SPLASH;
        _attractSlideTimer = millis();
    }

    void drawDemoOverlay(GFXcanvas16 &canvas) {
        canvas.setFont();
        canvas.setTextSize(1);
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.setCursor(4, ArcadeConfig::LANDSCAPE_HEIGHT - 10);
        canvas.print("DEMO");
        if ((millis() / 500) % 2 == 0) {
            canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
            canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH - 82, ArcadeConfig::LANDSCAPE_HEIGHT - 10);
            canvas.print("[BTN A] START");
        }
    }

    // While a demo runs, new sounds are dropped (lifted again whichever way
    // update() returns).
    struct Silence {
        AudioEngine &a; bool on;
        Silence(AudioEngine &a_, bool on_) : a(a_), on(on_) { if (on) a.setSilenced(true); }
        ~Silence() { if (on) a.setSilenced(false); }
    };

    // Spawn explosion particles and start non-blocking WAV stream.
    // Graphics continue normally — audio feeds through update() each frame.
    void triggerShipExplosion(AudioEngine &audio) {
        float cx = _ship.getX() + 8.0f;
        float cy = (float)_ship.getY() + 5.0f;
        _particles.spawnExplosion(cx, cy, ArcadeConfig::COLOR_ION_BLUE, 20, 600, 4);
        _particles.spawnExplosion(cx, cy, ArcadeConfig::COLOR_YELLOW, 10, 600, 4);
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
        _shipXOffset       = 0.0f;
        _shipYOffset       = (float)(ArcadeConfig::LANDSCAPE_HEIGHT / 2);
        _gameOverPending   = false;
        _demo              = false;
        // No start sound: the attract loop starts straight away.
        audio.preload("/audio/powerup.wav");   // every pickup; loaded now, not on the first
        _shotOnCard = audio.exists(SHOT_WAV);
        _hitOnCard  = audio.exists(HIT_WAV);
        if (_shotOnCard) audio.preload(SHOT_WAV);
        if (_hitOnCard)  audio.preload(HIT_WAV);
        _ufoOnCard = audio.exists(UFO_WAV);
        if (_ufoOnCard)  audio.preload(UFO_WAV);
    }

    void setTFT(Adafruit_ST7735 &tft) override { _tft = &tft; }
    bool flushesItself() const override { return true; }

    bool update(GFXcanvas16 &canvas,
                const InputState &input,
                AudioEngine &audio) override {

        // Quitting is the cabinet's Back button (main.cpp, then onQuit()).

        // ---- ATTRACT DEMO: A plays for real, time's up ends it ----
        if (_demo) {
            if (input.btnAPressed) { endDemo(); startNewGame(audio); return true; }
            if (millis() >= _demoUntil && _phase == PHASE_PLAYING) endDemo();
        }
        Silence silence(audio, _demo);
        const InputState in = (_demo && _phase == PHASE_PLAYING) ? demoPilot() : input;

        // ---- PHASE: ATTRACT ----
        if (_phase == PHASE_ATTRACT) {
            if (millis() - _attractSlideTimer > 8000) {
                if (_attractSlide != SLIDE_SCORES) {   // splash, how-to-play, scores, demo
                    _attractSlide      = _attractSlide == SLIDE_SPLASH ? SLIDE_INFO : SLIDE_SCORES;
                    _attractSlideTimer = millis();
                } else {
                    startDemo();                 // then back to the splash
                    return true;
                }
            }

            if (_attractSlide == SLIDE_SPLASH)    renderSplash(canvas);
            else if (_attractSlide == SLIDE_INFO) renderInfoScreen(canvas);
            else                                  renderScoresScreen(canvas);

            if (input.btnAPressed) {
                audio.stopLoop();
                startNewGame(audio);
            }

            flushLandscape(canvas);
            return true;
        }

        // ---- PHASE: PLAYING ----
        if (_phase == PHASE_PLAYING) {
            bool uiNeedsUpdate = false;
            bool playerHit     = false;

            _backdrop.update(_score);
            _particles.update();

            _ship.updatePosition(in.joyX);  // no-op, kept for compat

            // Y-axis: velocity-based
            // (joystick X/Y swapped and Y-direction inverted for this game's
            // physical orientation)
            _shipYOffset += in.joyX * SHIP_MOVE_SPEED;
            _shipYOffset  = constrain(_shipYOffset,
                                      (float)SHIP_Y_MIN,
                                      (float)SHIP_Y_MAX);
            _ship.setY((int)_shipYOffset);

            // X-axis: joystick X lets ship push into field up to 1/3 screen width
            _shipXOffset += in.joyY * SHIP_MOVE_SPEED;
            _shipXOffset  = constrain(_shipXOffset, 0.0f,
                                      (float)(SHIP_X_MAX - SHIP_X_MIN));
            _ship.setX((float)SHIP_X_MIN + _shipXOffset);

            _ship.updateAnimation();
            _ship.updateShield();

            // Exhaust: a spark off the engine flame each frame, streaking
            // back past the field as it scrolls by.
            _particles.spawnFire(_ship.getX(), (float)_ship.getY() + 4.5f,
                                 -random(8, 17) * 0.1f, random(-2, 3) * 0.05f, 0, 2);

            _powerUps.update(_score, _ship, _lives, uiNeedsUpdate, audio, _asteroids);
            _asteroids.update(_ship, _score, _asteroidsPassed, _nextTargetScore,
                              uiNeedsUpdate, playerHit, audio, _particles);

            // The saucer: maybe one with each Fire pickup.
            const bool fireOn = _ship.isFireActive();
            if (fireOn && !_fireWasOn) _saucer.fireStarted();
            _fireWasOn = fireOn;
            bool arrived, fired, shieldTook;
            if (_saucer.update(_ship, fireOn, _particles, arrived, fired, shieldTook)) playerHit = true;
            if (arrived) {                          // Fire refilled, to have time to bring it down
                _ship.activateFire(ArcadeConfig::FIRE_DURATION_MS);
                sfxSaucer(audio);
            }
            if (fired)      audio.playSound(320, 90);
            if (shieldTook) audio.playSound(300, 200);

            if (playerHit) {                         // a hit ends the Fire power-up, and the saucer
                _ship.deactivateFire();
                _bolts.clearAll();
                _saucer.reset();
                _fireWasOn = false;
            }
            if (playerHit && _demo) {                // a demo hit just ends the demo
                triggerShipExplosion(audio);
                _phase      = PHASE_HIT;
                _phaseTimer = millis();
                return true;
            }
            if (playerHit) {
                triggerShipExplosion(audio);
                _lives--;

                // Always go through PHASE_HIT first so explosion plays out.
                // _gameOverPending signals that PHASE_HIT should transition to
                // PHASE_GAMEOVER instead of respawning.
                _phase      = PHASE_HIT;
                _phaseTimer = millis();
                _asteroids.forceBoardWipe();

                if (_lives <= 0) {
                    _gameOverPending = true;
                    // Game-over sound plays after explosion settles in PHASE_HIT
                } else {
                    _gameOverPending = false;
                }

                // Render one explosion frame before phase switch
                _backdrop.render(canvas);
                _particles.render(canvas, 11);
                drawUI(canvas);
                flushLandscape(canvas);
                return true;
            }

            // Fire: A held shoots from the nose every FIRE_INTERVAL_MS
            if (_ship.isFireActive() && in.btnA && millis() >= _nextShotMs) {
                _bolts.fire(_ship.getX() + ArcadeConfig::SHIP_WIDTH - 1, (float)_ship.getY() + 4.0f);
                _nextShotMs = millis() + ArcadeConfig::FIRE_INTERVAL_MS;
                sfxShot(audio);
            }
            int saucerHit = 0;
            if (_bolts.update(_asteroids, _saucer, saucerHit, _score, _asteroidsPassed,
                              _nextTargetScore, _particles) > 0) {
                sfxHit(audio);
                uiNeedsUpdate = true;
            }
            if (saucerHit == 2) {                    // brought down
                _score += ArcadeConfig::SAUCER_SCORE;
                uiNeedsUpdate = true;
                audio.playExplosionSound(explosion_data, sizeof(explosion_data));
            } else if (saucerHit == 1) {
                audio.playSound(900, 40);
            }

            // Normal gameplay render: the backdrop clears the playfield
            _backdrop.render(canvas);
            _particles.render(canvas, 11);
            _powerUps.render(canvas);
            _asteroids.render(canvas);
            _saucer.render(canvas);
            _bolts.render(canvas);
            _ship.render(canvas);

            const bool fireBar = _ship.isFireActive();
            if (uiNeedsUpdate || _uiDirty || (_fireBarShown && !fireBar)) {
                drawUI(canvas);
                _uiDirty = false;
            }
            if (fireBar) drawFireBar(canvas);
            _fireBarShown = fireBar;
            if (_demo) drawDemoOverlay(canvas);

            flushLandscape(canvas);
            return true;
        }

        // ---- PHASE: HIT — show explosion, then respawn or game over ----
        if (_phase == PHASE_HIT) {
            _particles.update();
            _backdrop.render(canvas);            // held still while the ship breaks up
            _particles.render(canvas, 11);
            drawUI(canvas);
            if (_demo) drawDemoOverlay(canvas);
            flushLandscape(canvas);

            if (_demo) {
                if (millis() - _phaseTimer > 800) endDemo();
                return true;
            }

            if (millis() - _phaseTimer > 800) {
                _particles.clearAll();
                if (_gameOverPending) {
                    // Explosion done: a name for the table if the score
                    // made it, then the game-over sound and screen.
                    _scores.forget();
                    _phase             = _scores.offer(_score) ? PHASE_NAME : PHASE_GAMEOVER;
                    _phaseTimer        = millis();
                    _gameOverEnteredMs = millis();
                    _gameOverPending   = false;
                    audio.stopLoop();
                    audio.playGameOverSound(gameend_data, sizeof(gameend_data));
                } else {
                    // Respawn straight into play, shielded for a moment
                    _phase      = PHASE_PLAYING;
                    _phaseTimer = millis();
                    _ship.activateShield(SPAWN_SHIELD_MS);
                }
            }
            return true;
        }

        // ---- PHASE: NAME ENTRY ----
        if (_phase == PHASE_NAME) {
            canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
            _scores.draw(canvas);
            flushLandscape(canvas);
            if (_scores.update(input, getRotation())) {
                _highScore         = (int)_scores.best();
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
            canvas.print("SCORE: "); canvas.print(_score);

            const int rank = _scores.lastRank();
            char hiBuf[24];
            canvas.setTextColor(rank >= 0 ? ArcadeConfig::COLOR_GREEN : ArcadeConfig::COLOR_GREY);
            canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH / 4, 65);
            if (rank == 0) canvas.print("NEW HIGH SCORE!!");
            else if (rank > 0) { snprintf(hiBuf, sizeof(hiBuf), "HIGH SCORE #%d", rank + 1); canvas.print(hiBuf); }
            else canvas.print(_scores.bestLine(hiBuf, sizeof(hiBuf), "BEST: "));

            canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
            canvas.setCursor(20, 90);
            canvas.print("[BTN A] PLAY AGAIN");
            canvas.setCursor(20, 103);
            canvas.print("[HOLD BACK] QUIT");

            // Show countdown in last 10 seconds before attract timeout
            unsigned long elapsed = millis() - _gameOverEnteredMs;
            if (elapsed > (GAMEOVER_TIMEOUT_MS - 10000UL)) {
                int secsLeft = (int)((GAMEOVER_TIMEOUT_MS - elapsed) / 1000UL) + 1;
                canvas.setTextColor(ArcadeConfig::COLOR_AMBER);
                canvas.setCursor(20, 116);
                canvas.print("AUTO: "); canvas.print(secsLeft); canvas.print("s");
            }

            flushLandscape(canvas);

            // A plays again, only after the input delay, so mashing at the
            // end doesn't.
            const bool inputOk = elapsed >= ArcadeConfig::GAMEOVER_INPUT_DELAY_MS;
            if (inputOk && input.btnAPressed) {
                startNewGame(audio);
                return true;
            }

            // 30-second timeout: return to attract mode
            if (elapsed > GAMEOVER_TIMEOUT_MS) {
                _phase               = PHASE_ATTRACT;
                _attractSlide        = SLIDE_SPLASH;
                _attractSlideTimer   = millis();
            }

            return true;
        }

        return true;
    }

    // Quitting (the Back button): a game in progress still goes on the
    // table, under the last name entered; a name being entered is kept.
    void onQuit(AudioEngine &audio) override {
        if (_phase == PHASE_NAME) _scores.finishNow();
        else if (!_demo && (_phase == PHASE_PLAYING || _phase == PHASE_HIT)) _scores.record(_score);
        audio.mute();
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