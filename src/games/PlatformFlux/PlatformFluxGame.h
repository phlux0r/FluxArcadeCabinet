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

    enum AttractSlide { SLIDE_SPLASH, SLIDE_INFO };
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

public:
    PlatformFluxGame() {}

    void init(AudioEngine &audio) override {
        loadHighScore();
        _phase              = PHASE_ATTRACT;
        _attractSlide       = SLIDE_SPLASH;
        _attractSlideTimer  = millis();
        _btnBWasHeld        = true;
        audio.playLanderStartSound(); // shared game-select jingle, same as Lander/Maze
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
            if (millis() - _attractSlideTimer > ArcadeConfig::ATTRACT_MODE_TIMER) {
                _attractSlide      = (_attractSlide == SLIDE_SPLASH) ? SLIDE_INFO : SLIDE_SPLASH;
                _attractSlideTimer = millis();
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
            if (input.btnAPressed) _jumpPressedAt = millis();
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
            _playerXOffset += input.joyY * ArcadeConfig::RUNNER_X_MOVE_SPEED;
            _playerXOffset  = constrain(_playerXOffset,
                                        (float)ArcadeConfig::RUNNER_X_MIN_OFFSET,
                                        (float)ArcadeConfig::RUNNER_X_MAX_OFFSET);
            _player.setX((float)ArcadeConfig::RUNNER_BASE_X + _playerXOffset);

            // While levitating, joyX (otherwise unused in this game) drives
            // free vertical movement instead of gravity/ground collision.
            if (_player.isLevitating()) {
                _player.moveVertical(input.joyX * ArcadeConfig::RUNNER_LEVITATE_SPEED);
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
                _lives--;
                _diedThisStage = true;
                _bannerUntil = 0;
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
            _levitationPowerUp.render(canvas);
            _enemies.render(canvas, _platforms.getLoop());
            _boulders.render(canvas, _platforms, _platforms.getLoop());
            _player.render(canvas);
            drawBanner(canvas);

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
            flushLandscape(canvas);

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
