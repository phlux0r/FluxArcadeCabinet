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
#include "LevitationPowerUpManager.h"
#include "RollingBoulderManager.h"
#include "assets/TitleScreen.h"

#include <Preferences.h>

class PlatformFluxGame : public IGame {
private:
    PlayerRunner          _player;
    PlatformManager       _platforms;
    FlyingEnemyManager    _enemies;
    RunnerPowerUpManager  _powerUp;
    LevitationPowerUpManager _levitationPowerUp;
    RollingBoulderManager _boulders;
    ParticleManager       _particles;

    Preferences      _prefs;
    Adafruit_ST7735* _tft = nullptr;

    int _score     = 0;
    int _highScore = 0;
    float _playerXOffset = 0.0f;

    // Lives and stages (see ArcadeConfig's RUNNER_LIVES block).
    int  _lives = ArcadeConfig::RUNNER_LIVES;
    int  _stage = 1;
    bool _diedThisStage = false;
    int  _loopsSeen = 0;
    unsigned long _jumpPressedAt = 0;      // jump buffer; 0 = none pending

    // Centre-screen banner: "STAGE 3", a bonus line, "EXTRA LIFE".
    char _bannerTitle[20] = "";
    char _bannerSub[24]   = "";
    uint16_t _bannerSubColor = ArcadeConfig::COLOR_GREEN;
    unsigned long _bannerUntil = 0;

    enum GamePhase { PHASE_ATTRACT, PHASE_PLAYING, PHASE_DEATH, PHASE_GAMEOVER };
    GamePhase _phase = PHASE_ATTRACT;

    // Title, how-to-play, then the autopilot demo (a run with _demo set).
    enum AttractSlide { SLIDE_SPLASH, SLIDE_INFO };
    bool _demo = false;
    unsigned long _demoUntil = 0;
    AttractSlide  _attractSlide      = SLIDE_SPLASH;
    unsigned long _attractSlideTimer = 0;

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

    // Stage, lives, score and best along the top; the rule under them is
    // the stage progress bar (green up to how far through the stage you are).
    void drawUI(GFXcanvas16 &canvas) {
        canvas.fillRect(0, 0, ArcadeConfig::LANDSCAPE_WIDTH, 10, ArcadeConfig::COLOR_BLACK);
        int done = (int)(ArcadeConfig::LANDSCAPE_WIDTH * _platforms.stageProgress());
        canvas.drawFastHLine(0, 10, ArcadeConfig::LANDSCAPE_WIDTH, ArcadeConfig::COLOR_GREY);
        if (done > 0) canvas.drawFastHLine(0, 10, done, ArcadeConfig::COLOR_GREEN);

        canvas.setTextSize(1);
        canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
        canvas.setCursor(2, 1);
        canvas.print("ST"); canvas.print(_stage);

        // Lives as small blocks in the runner's colours.
        int lx = _stage >= 10 ? 29 : 23;
        for (int i = 0; i < _lives; i++) {
            canvas.fillRect(lx + i * 5, 2, 3, 6, ArcadeConfig::COLOR_ORANGE);
        }

        canvas.setTextColor(ArcadeConfig::COLOR_YELLOW);
        canvas.setCursor(58, 1);
        canvas.print(_score);

        canvas.setTextColor(ArcadeConfig::COLOR_GREY);
        canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH - 54, 1);
        canvas.print("HI:"); canvas.print(_highScore);
    }

    void showBanner(const char* title, const char* sub, uint16_t subColor) {
        snprintf(_bannerTitle, sizeof(_bannerTitle), "%s", title);
        snprintf(_bannerSub, sizeof(_bannerSub), "%s", sub);
        _bannerSubColor = subColor;
        _bannerUntil = millis() + ArcadeConfig::RUNNER_BANNER_MS;
    }

    void drawBanner(GFXcanvas16 &canvas) {
        if (millis() >= _bannerUntil) return;
        int16_t x1, y1;
        uint16_t w, h;
        canvas.setTextSize(1);
        canvas.getTextBounds(_bannerTitle, 0, 0, &x1, &y1, &w, &h);
        canvas.fillRect((ArcadeConfig::LANDSCAPE_WIDTH - w) / 2 - 3, 27, w + 6, _bannerSub[0] ? 22 : 12,
                        ArcadeConfig::COLOR_BLACK);
        canvas.setTextColor(ArcadeConfig::COLOR_YELLOW);
        canvas.setCursor((ArcadeConfig::LANDSCAPE_WIDTH - w) / 2, 29);
        canvas.print(_bannerTitle);
        if (_bannerSub[0]) {
            canvas.getTextBounds(_bannerSub, 0, 0, &x1, &y1, &w, &h);
            canvas.fillRect((ArcadeConfig::LANDSCAPE_WIDTH - w) / 2 - 3, 37, w + 6, 12, ArcadeConfig::COLOR_BLACK);
            canvas.setTextColor(_bannerSubColor);
            canvas.setCursor((ArcadeConfig::LANDSCAPE_WIDTH - w) / 2, 39);
            canvas.print(_bannerSub);
        }
    }

    void renderSplash(GFXcanvas16 &canvas) {
        for (int i = 0; i < FLUX_RUNNER_128X160_WIDTH * FLUX_RUNNER_128X160_HEIGHT; i++) {
            uint16_t px = pgm_read_word(&flux_runner_128x160_data[i]);
            canvas.drawPixel(i % FLUX_RUNNER_128X160_WIDTH,
                             i / FLUX_RUNNER_128X160_WIDTH, px);
        }

        // The art's bottom 16px is a dark strip reserved for HUD-style text —
        // best distance on the left, start prompt on the right, both
        // vertically centered in that strip.
        int stripY = ArcadeConfig::LANDSCAPE_HEIGHT - 16;
        canvas.setTextSize(1);
        canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
        canvas.setCursor(4, stripY + 4);
        canvas.print("BEST:"); canvas.print(_highScore);
        canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH - 80, stripY + 4);
        canvas.print("[BTN A] START");
    }

    void renderInfoScreen(GFXcanvas16 &canvas) {
        canvas.fillScreen(ArcadeConfig::COLOR_BLACK);

        canvas.setTextSize(1);
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.setCursor(30, 2);
        canvas.print("--== HOW TO PLAY ==--");

        canvas.setTextColor(ArcadeConfig::COLOR_AMBER);
        canvas.setCursor(6, 13);
        canvas.print("[JOY] SHIFT FWD/BACK");
        canvas.setCursor(6, 23);
        canvas.print("[BTN A] JUMP");

        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.setCursor(6, 35);
        canvas.print("HAZARDS:");

        canvas.setTextColor(ArcadeConfig::COLOR_ORANGE);
        canvas.setCursor(10, 45);
        canvas.print("FIRE PITS");
        canvas.setTextColor(ArcadeConfig::COLOR_GREEN);
        canvas.setCursor(10, 55);
        canvas.print("PLATFORMS & GAPS");
        canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
        canvas.setCursor(10, 65);
        canvas.print("MOVING PLATFORMS");
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.setCursor(10, 75);
        canvas.print("STAIRS & SPIKE TRAPS");
        canvas.setTextColor(ArcadeConfig::COLOR_AMBER);
        canvas.setCursor(10, 85);
        canvas.print("ROLLING BOULDERS");
        canvas.setTextColor(ArcadeConfig::COLOR_MAGENTA);
        canvas.setCursor(10, 95);
        canvas.print("FLYING ENEMY + ROCKS");

        canvas.setTextColor(ArcadeConfig::COLOR_YELLOW);
        canvas.setCursor(6, 104);
        canvas.print("STAR: INVINCIBLE");
        canvas.setTextColor(ArcadeConfig::COLOR_ION_BLUE);
        canvas.setCursor(6, 112);
        canvas.print("DIAMOND: FLY 10s");

        canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
        canvas.setCursor(24, 120);
        canvas.print("[BTN A] TO START");
    }

    void startNewGame(AudioEngine &audio) {
        // Cut off whatever's still playing (e.g. the game-over sound, if
        // "play again" was pressed before it finished) so it doesn't keep
        // running into the new game.
        audio.mute();
        _score = 0;
        _lives = ArcadeConfig::RUNNER_LIVES;
        _loopsSeen = 0;
        startStage(1);
        // Music plays during a game only, not on the attract screen; it
        // carries on through lost lives and stops at game over.
        audio.loopWAV("/audio/flux-runner.wav");
    }

    // (Re)starts the world at `stage`: its terrain, hazards and speed, on a
    // safe ledge. Used for a new game and after each lost life.
    void startStage(int stage) {
        _stage = stage;
        _diedThisStage = false;
        _jumpPressedAt = 0;
        _particles.clearAll();
        _platforms.initGame(stage);
        _enemies.initGame();
        _boulders.initGame();
        _powerUp.reset();
        _levitationPowerUp.reset();
        _playerXOffset = 0.0f;
        _player.reset((float)ArcadeConfig::RUNNER_BASE_X, ArcadeConfig::LANDSCAPE_HEIGHT - 8);
        _uiDirty = true;
        _phase = PHASE_PLAYING;
        _phaseTimer = millis();
        char title[20];
        snprintf(title, sizeof(title), "STAGE %d", stage);
        showBanner(title, "", ArcadeConfig::COLOR_GREEN);
    }

    // Called each frame: a new stage reached means the last one was cleared.
    void checkStageProgress(AudioEngine &audio) {
        int now = _platforms.stageNumber();
        if (now <= _stage) return;
        bool perfect = !_diedThisStage;
        int bonus = ArcadeConfig::RUNNER_STAGE_BONUS * (perfect ? 2 : 1);
        _score += bonus;
        _stage = now;
        _diedThisStage = false;
        _uiDirty = true;

        char title[20], sub[24];
        snprintf(title, sizeof(title), "STAGE %d", _stage);
        snprintf(sub, sizeof(sub), perfect ? "PERFECT +%d" : "CLEAR +%d", bonus);
        showBanner(title, sub, perfect ? ArcadeConfig::COLOR_GREEN : ArcadeConfig::COLOR_WHITE);

        // A whole loop of stages done: an extra life.
        if (_platforms.loopsCompleted() > _loopsSeen) {
            _loopsSeen = _platforms.loopsCompleted();
            if (_lives < ArcadeConfig::RUNNER_MAX_LIVES) {
                _lives++;
                snprintf(sub, sizeof(sub), "EXTRA LIFE +%d", bonus);
                showBanner(title, sub, ArcadeConfig::COLOR_ORANGE);
                static const int n[] = { 880, 1175, 1568, 2093 };
                static const int d[] = {  70,   70,   70,  200 };
                audio.playMelody(n, d, 4);
                return;
            }
        }
        static const int n[] = { 784, 988, 1175 };
        static const int d[] = {  60,  60,  140 };
        audio.playMelody(n, d, 3);
    }

    void triggerPlayerDeath(AudioEngine &audio) {
        _particles.triggerExplosion(_player.getX() + RUNNER_WIDTH / 2.0f,
                                     _player.getY() + RUNNER_HEIGHT / 2.0f);
        audio.playDeathSound();
    }

    // ---- Attract demo --------------------------------------------------------

    // The autopilot's stick: forward in the air (a fire pit is wider than a
    // standing jump carries the runner's whole body, so pushing forward
    // through a jump is what clears one, for the bot as for a player), and
    // on the ground `groundStick`: normally back, keeping the most room to
    // go forward, or forward to step out from under a falling rock.
    static float demoStick(bool onGround, float groundStick) { return onGround ? groundStick : 1.0f; }

    // The runner as the autopilot's prediction sees it.
    struct DemoState { float off, y, vy; bool onGround, canJump; };
    // Predicted frames the autopilot may spend deciding one frame's input:
    // a hopeless spot can otherwise try hundreds of two-jump plans, which
    // on the ESP32 would stall a frame. Out of budget counts as "no".
    static const int DEMO_STEP_BUDGET = 2500;
    mutable int _demoBudget = 0;

    // Does the runner come through to frame `horizon` alive if it jumps at
    // frame `jumpAt` (-1: doesn't), starting from `st` at frame `t0`? A
    // prediction on the game's own rules: the same physics, stick movement,
    // ground test, fire pits, spikes (all treated as live) and boulders,
    // with the ground scrolling at today's speed. A jump must land and stay
    // down a few frames, and then, `depth` more moves deep, still have a
    // way on: running on, or another jump that works. (Without that it will
    // happily land just in front of a spike it can't then clear.)
    bool demoSurvives(DemoState st, int t0, int jumpAt, int horizon, int depth,
                      float groundStick = -1.0f) const {
        const float s = _platforms.getScrollSpeed();
        bool jumped = false;
        int landed = 0;
        for (int t = t0 + 1; t <= horizon; ++t) {
            if (--_demoBudget < 0) return false;
            const float stick = demoStick(st.onGround, groundStick);   // read before this frame's jump, as in play
            if (t - 1 == jumpAt && (t == t0 + 1 ? st.canJump : st.onGround)) {
                st.vy = -ArcadeConfig::RUNNER_JUMP_VELOCITY;
                st.onGround = false;
                jumped = true;
            }
            st.off = constrain(st.off + stick * ArcadeConfig::RUNNER_X_MOVE_SPEED,
                               (float)ArcadeConfig::RUNNER_X_MIN_OFFSET, (float)ArcadeConfig::RUNNER_X_MAX_OFFSET);
            const float x = (float)ArcadeConfig::RUNNER_BASE_X + st.off;
            const float shift = s * (float)t;
            const float px = x + shift, pr = px + RUNNER_WIDTH, pb = st.y + RUNNER_HEIGHT;
            if (_platforms.firePitHitsPlayer(px, pr, pb)) return false;
            if (_platforms.spikeNear(px, pr, pb)) return false;
            int g = _platforms.groundYAt(px, pr, st.y, pb, t);
            float gy = (g == -1) ? (float)(ArcadeConfig::LANDSCAPE_HEIGHT + 40) : (float)g;
            st.vy += ArcadeConfig::RUNNER_GRAVITY;
            st.y += st.vy;
            if (st.y + RUNNER_HEIGHT >= gy && st.vy >= 0.0f) {
                st.y = gy - RUNNER_HEIGHT;
                st.vy = 0.0f;
                st.onGround = true;
            } else {
                st.onGround = false;
            }
            st.canJump = st.onGround;
            if (st.y > ArcadeConfig::LANDSCAPE_HEIGHT) return false;
            // Boulders and rocks move and hit after the runner has, as in play.
            if (_boulders.wouldHit(t, s, x, x + RUNNER_WIDTH, st.y, st.y + RUNNER_HEIGHT, _platforms)) return false;
            if (_enemies.rockWouldHit(t, x, x + RUNNER_WIDTH, st.y, st.y + RUNNER_HEIGHT)) return false;
            if (jumped && st.onGround && ++landed >= 4) {
                if (depth <= 0) return true;
                if (demoSurvives(st, t, -1, horizon, depth - 1)) return true;
                for (int k = t; k < horizon - 4; k += 3) {
                    if (demoSurvives(st, t, k, horizon, depth - 1)) return true;
                }
                return false;
            }
        }
        return !jumped;
    }

    bool demoSurvives(int jumpAt, int horizon, float groundStick = -1.0f) const {
        DemoState st{ _playerXOffset, _player.getY(), _player.getVy(),
                      _player.isOnGround(), _player.canJump() };
        return demoSurvives(st, 0, jumpAt, horizon, 1, groundStick);
    }

    // The autopilot: keeps running while that's safe (stepping forward
    // instead if that's what dodges a rock), and otherwise jumps at the
    // earliest good moment, so it lands just past a hazard with room for
    // the next one, or at the last one if the window's short. Levitating, it
    // cruises high and comes down onto ground as it ends.
    InputState demoPilot() const {
        InputState in{};
        const int H = ArcadeConfig::RUNNER_DEMO_LOOKAHEAD;
        _demoBudget = DEMO_STEP_BUDGET;
        if (_player.isLevitating()) {
            float targetY = 30.0f;
            if (_player.levitationLeftMs() < 1500) {
                float px = _player.getX();
                targetY = (float)_platforms.surfaceYNear(px, px + RUNNER_WIDTH) - RUNNER_HEIGHT;
            }
            in.joyX = constrain((targetY - _player.getY()) / 8.0f, -1.0f, 1.0f);
            return in;
        }
        in.joyY = demoStick(_player.isOnGround(), -1.0f);
        if (!_player.canJump() || demoSurvives(-1, H)) return in;
        if (_player.isOnGround() && demoSurvives(-1, H, 1.0f)) { in.joyY = 1.0f; return in; }
        const bool now = demoSurvives(0, H);
        if ((now && demoSurvives(2, H)) ||          // early, with a margin
            (now && !demoSurvives(1, H)) ||         // or the last chance
            (!now && !demoSurvives(-1, 3))) {       // or nothing better
            in.btnAPressed = true;
        }
        return in;
    }

    // A game's world at a random stage, played by demoPilot().
    void startDemo() {
        _demo = true;
        _score = 0;
        _lives = ArcadeConfig::RUNNER_LIVES;
        _loopsSeen = 0;
        startStage((int)random(ArcadeConfig::RUNNER_DEMO_MIN_STAGE, ArcadeConfig::RUNNER_DEMO_MAX_STAGE + 1));
        _loopsSeen = _platforms.loopsCompleted();
        _demoUntil = millis() + (unsigned long)random((long)ArcadeConfig::RUNNER_DEMO_MIN_MS,
                                                      (long)ArcadeConfig::RUNNER_DEMO_MAX_MS + 1);
    }

    // Back to the title, leaving nothing of the demo behind.
    void endDemo() {
        _demo = false;
        _particles.clearAll();
        _bannerUntil = 0;
        _score = 0;
        _phase = PHASE_ATTRACT;
        _attractSlide = SLIDE_SPLASH;
        _attractSlideTimer = millis();
    }

    void drawDemoOverlay(GFXcanvas16 &canvas) {
        canvas.setTextSize(1);
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH - 28, 14);
        canvas.print("DEMO");
        if ((millis() / 500) % 2 == 0) {
            canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
            canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH / 2 - 45, 118);
            canvas.print("[BTN A] START");
        }
    }

    // While a demo runs, new sounds are dropped (and the guard always lifts
    // the silence again, whichever way update() returns).
    struct Silence {
        AudioEngine &a; bool on;
        Silence(AudioEngine &a_, bool on_) : a(a_), on(on_) { if (on) a.setSilenced(true); }
        ~Silence() { if (on) a.setSilenced(false); }
    };

public:
    PlatformFluxGame() {}

    void init(AudioEngine &audio) override {
        loadHighScore();
        _phase              = PHASE_ATTRACT;
        _attractSlide       = SLIDE_SPLASH;
        _attractSlideTimer  = millis();
        _btnBWasHeld        = true;
        _demo               = false;
        audio.playLanderStartSound(); // shared game-select jingle, same as Lander/Maze
    }

    void setTFT(Adafruit_ST7735 &tft) override { _tft = &tft; }
    bool flushesItself() const override { return true; }

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

        // ---- ATTRACT DEMO: A plays for real, B leaves, time's up ends it ----
        if (_demo) {
            if (input.btnAPressed) { endDemo(); startNewGame(audio); return true; }
            if (input.btnBPressed) { endDemo(); audio.mute(); return false; }
            if (millis() >= _demoUntil && _phase == PHASE_PLAYING) { endDemo(); return true; }
        }
        Silence silence(audio, _demo);
        const InputState in = _demo ? demoPilot() : input;

        // ---- PHASE: ATTRACT ----
        if (_phase == PHASE_ATTRACT) {
            if (millis() - _attractSlideTimer > ArcadeConfig::ATTRACT_MODE_TIMER) {
                if (_attractSlide == SLIDE_SPLASH) {
                    _attractSlide      = SLIDE_INFO;
                    _attractSlideTimer = millis();
                } else {
                    startDemo();                 // then back to the splash
                    return true;
                }
            }

            if (_attractSlide == SLIDE_SPLASH) renderSplash(canvas);
            else                                renderInfoScreen(canvas);

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
            checkStageProgress(audio);

            // Terrain/hazard progression (see PlatformManager class comment
            // and ArcadeConfig's RUNNER_*_TIER constants for the full map):
            // tier 0/1 ground+fire pits -> tier 2 platform gaps -> tier 3
            // + moving platforms -> tier 4 + flying enemy (early preview)
            // -> tier 5 ground again with stairs+spikes, no ships -> tier 6
            // + rolling boulders, still no ships -> tier 7 ships return.
            // The enemy is capped at 1 and toggles on/off with tier rather
            // than unlocking once, so it can step aside for tiers 5-6.
            int tier = _platforms.getTier();
            bool shipsActive = (tier == ArcadeConfig::RUNNER_EARLY_SHIP_TIER) ||
                               (tier >= ArcadeConfig::RUNNER_ENEMY_TIER);
            _enemies.setActive(shipsActive);

            // Jump buffer: a press just before landing still jumps on landing.
            if (in.btnAPressed) _jumpPressedAt = millis();
            if (_jumpPressedAt != 0) {
                if (millis() - _jumpPressedAt > ArcadeConfig::RUNNER_JUMP_BUFFER_MS) {
                    _jumpPressedAt = 0;
                } else if (_player.jump()) {
                    _jumpPressedAt = 0;
                    audio.playJumpSound();
                }
            }

            // Joystick nudges the runner forward/back within a bounded range —
            // rotation-1 games read joyY for on-screen horizontal, same swap
            // AsteroidFlux uses for its physical orientation.
            _playerXOffset += in.joyY * ArcadeConfig::RUNNER_X_MOVE_SPEED;
            _playerXOffset  = constrain(_playerXOffset,
                                        (float)ArcadeConfig::RUNNER_X_MIN_OFFSET,
                                        (float)ArcadeConfig::RUNNER_X_MAX_OFFSET);
            _player.setX((float)ArcadeConfig::RUNNER_BASE_X + _playerXOffset);

            // While levitating, joyX (otherwise unused in this game) drives
            // free vertical movement instead of gravity/ground collision.
            if (_player.isLevitating()) {
                _player.moveVertical(in.joyX * ArcadeConfig::RUNNER_LEVITATE_SPEED);
                if (millis() % 120 < 20) {
                    _particles.spawnFire(_player.getX() + RUNNER_WIDTH / 2.0f,
                                         _player.getY() + RUNNER_HEIGHT,
                                         0.0f, 0.3f, ArcadeConfig::COLOR_ION_BLUE);
                }
            }
            // If levitation just ended, physics below resumes falling
            // naturally from wherever the player currently is.
            _player.updateLevitation();

            float px = _player.getX(), pRight = px + RUNNER_WIDTH;
            float py = _player.getY(), pBottom = py + RUNNER_HEIGHT;
            int ground = _platforms.groundYAt(px, pRight, py, pBottom);
            float groundTarget = (ground == -1) ? (float)(ArcadeConfig::LANDSCAPE_HEIGHT + 40) : (float)ground;

            _player.updatePhysics(groundTarget);
            _player.updateAnimation();
            _player.updateInvincibility();

            _powerUp.maybeSpawn(tier, ArcadeConfig::LANDSCAPE_WIDTH, _platforms);
            _powerUp.update(_platforms.getScrollSpeed(), _player, _particles, audio, uiNeedsUpdate);

            float firePitX;
            bool hasFirePitAhead = _platforms.upcomingFirePitX(firePitX);
            _levitationPowerUp.maybeSpawn(tier, ArcadeConfig::LANDSCAPE_WIDTH, _platforms,
                                          hasFirePitAhead, firePitX);
            _levitationPowerUp.update(_platforms.getScrollSpeed(), _player, _particles, audio, uiNeedsUpdate, _platforms);

            _enemies.update(_platforms.getScrollSpeed(), _player, _particles, audio, playerHit);
            _boulders.update(tier, _platforms.getScrollSpeed(), _platforms, _player, _particles, audio, playerHit);

            // Spike traps: contact damage, same invincibility rules as
            // enemies/boulders (levitating above one is naturally safe —
            // spikeHitsPlayer only counts feet near ground level).
            if (_platforms.spikeHitsPlayer(px, pRight, pBottom) && !_player.isInvincible()) {
                _particles.spawnExplosion(px + RUNNER_WIDTH / 2.0f, pBottom, ArcadeConfig::COLOR_WHITE, 6);
                playerHit = true;
            }

            // Fire pits: direct contact kill, separate from the fall-through
            // mechanic — catches a mistimed jump that lands straddling the
            // pit's edge (half on solid ground, half over the flame), which
            // groundYAt() alone could still read as "grounded" on the solid
            // half.
            if (_platforms.firePitHitsPlayer(px, pRight, pBottom) && !_player.isInvincible()) {
                _particles.spawnExplosion(px + RUNNER_WIDTH / 2.0f, pBottom, ArcadeConfig::COLOR_ORANGE, 6);
                playerHit = true;
            }

            // Falling off the bottom of the screen is always fatal — invincibility
            // only protects against enemy/rock contact, never a bottomless pit.
            // (Checked against the raw screen edge, not a padded margin, so a
            // fall is caught the moment it happens instead of some frames later.)
            bool fellOffScreen = _player.getY() > ArcadeConfig::LANDSCAPE_HEIGHT;

            // Score ticks with distance travelled (matches PlatformManager's
            // internal distance counter so difficulty and score stay in sync).
            static unsigned long lastScoreTick = 0;
            if (millis() - lastScoreTick > 100) {
                _score++;
                lastScoreTick = millis();
                uiNeedsUpdate = true;
            }

            if (fellOffScreen || (playerHit && !_player.isInvincible())) {
                triggerPlayerDeath(audio);
                _bannerUntil = 0;
                if (!_demo) {                // a demo death just ends the demo
                    _lives--;
                    _diedThisStage = true;
                    if (_score > _highScore) { _highScore = _score; saveHighScore(); }
                }
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
            _levitationPowerUp.render(canvas);
            _enemies.render(canvas, _platforms.getLoop());
            _boulders.render(canvas, _platforms, _platforms.getLoop());
            _player.render(canvas);
            drawBanner(canvas);
            if (_demo) drawDemoOverlay(canvas);

            // The progress rule moves every frame, so the HUD redraws every
            // frame now rather than only when uiNeedsUpdate/_uiDirty say so.
            (void)uiNeedsUpdate;
            drawUI(canvas);
            _uiDirty = false;

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
            if (_demo) drawDemoOverlay(canvas);
            flushLandscape(canvas);

            if (_demo) {
                if (millis() - _phaseTimer > 800) endDemo();
                return true;
            }
            if (millis() - _phaseTimer > 800 && _lives > 0) {
                // Back to the start of the stage you died in, briefly shielded.
                int stage = _stage;
                bool died = _diedThisStage;
                startStage(stage);
                _diedThisStage = died;
                _player.activateInvincibility(ArcadeConfig::RUNNER_RESPAWN_SHIELD_MS);
                return true;
            }
            if (millis() - _phaseTimer > 800) {
                _particles.clearAll();
                _phase             = PHASE_GAMEOVER;
                _gameOverEnteredMs = millis();
                audio.stopLoop();
                audio.playGameOverToneSound();
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
            canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH / 4, 55);
            canvas.print("STAGE: "); canvas.print(_stage);

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

            // Only after the input delay, so mashing at the end doesn't.
            const bool inputOk = elapsed >= ArcadeConfig::GAMEOVER_INPUT_DELAY_MS;
            if (inputOk && input.btnAPressed) { startNewGame(audio); return true; }
            if (inputOk && input.btnBPressed) { audio.mute(); return false; }

            if (elapsed > GAMEOVER_TIMEOUT_MS) {
                _phase       = PHASE_ATTRACT;
                _btnBWasHeld = false;
            }
            return true;
        }

        return true;
    }

    uint8_t getRotation() const override { return 1; }
    const char* getName()  const override { return "Flux Runner"; }

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
