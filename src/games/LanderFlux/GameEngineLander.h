#ifndef GAME_ENGINE_LANDER_H
#define GAME_ENGINE_LANDER_H

#include <Adafruit_GFX.h>
#include <Preferences.h>
#include "../../cabinet/ArcadeConfig.h"
#include "../../cabinet/ParticleManager.h"
#include "../../cabinet/AudioEngine.h"
#include "../../cabinet/HighScores.h"
#include "CavernObstacles.h"
#include "Ship.h"
#include "assets/TitleScreen.h"
#include "../../assets/shared/SharedAssets.h"

// =============================================================================
// GAME ENGINE — LANDER FLUX
//
// PHYSICS APPROACH: fixed 50Hz steps, matching the original delay(20).
// update() runs at the frame rate; physics steps once per 20ms of real
// time (catching up after a slow frame), so the original per-step
// constants (gravity, thrust=0.09, fuel-=0.4) behave as in the standalone
// version whatever the frame rate. No delta-time needed.
//
// TITLE SCREEN: Bitmap is blitted to TFT only when blink state changes
// (once per 600ms) — not every frame — so the startup melody doesn't
// cause visible re-rendering on every note.
// =============================================================================

// Physics tick interval — matches original delay(20) = 50fps
static const unsigned long PHYSICS_TICK_MS = 20UL;

class GameEngineLander {
private:
    // The cabinet's table for this game; _highScore is its top score.
    hiscore::ScoreBoard _scores;
    bool _naming = false;            // entering a name for the table, after the last crash
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
    // Between-level and game-over screens: A counts only once it has been
    // up after GAMEOVER_INPUT_DELAY_MS, so thrust held or mashed at the end
    // doesn't skip them.
    bool  _endInputArmed = false;
    bool  _titleAWasHeld = true;    // title: A starts only once it has been up
    bool  _isTitleScreen = true;

    unsigned long _attractModeTimer    = 0;
    bool          _showInstructionPage = _showScoresPage = false;
    bool          _showScoresPage = false;      // after how-to-fly: the high scores

    float _currentGravity  = 0.025f;
    bool  _fuelTankActive  = false;
    float _fuelTankX       = 0.0f;
    float _fuelTankY       = 0.0f;
    const int _fuelTankRadius = 4;

    static const int GROUND_SEGMENTS = 9;
    int _groundY[GROUND_SEGMENTS];
    int _groundStepX;
    int _padX;
    const int _padWidth = 24;

    // Button B: release guard, then hold EXIT_HOLD_MS to leave
    bool _btnBWasHeld = false;
    unsigned long _btnBHoldStart = 0;
    static const unsigned long EXIT_HOLD_MS = 2000UL;

    // Game-over attract timeout
    unsigned long _gameOverEnteredMs = 0;
    static const unsigned long GAMEOVER_TIMEOUT_MS = 30000UL;

    // Physics throttle — only tick physics every 20ms (matches original delay(20))
    unsigned long _lastPhysicsTick = 0;

    bool _thrustSoundActive = false;

    // ---- Attract demo ---------------------------------------------------------
    // Title, how-to-fly, then the autopilot flying real levels, silently, for
    // DEMO_MIN..MAX_MS or until it crashes. A starts a real game.
    bool _demo = false;
    unsigned long _demoUntil = 0;
    int  _demoLandings = 0;
    float _demoDescent = 0.6f;          // speed it lands at (over 1.1 crashes)
    float _demoThrustAcc = 0.0f;        // banked thrust, see demoPilot()
    bool _prevBtnA = false, _prevBtnB = false;
    static const unsigned long DEMO_MIN_MS = 30000, DEMO_MAX_MS = 40000;
    static const unsigned long DEMO_LANDED_MS = 2000;   // success screen, then on
    static const int DEMO_CELL = 4;                     // route grid, px
    static const int DEMO_GW = 32, DEMO_GH = 40;        // 128x160 in cells
    static const int DEMO_PATH_MAX = 96;
    int16_t _demoPathX[DEMO_PATH_MAX], _demoPathY[DEMO_PATH_MAX];
    int _demoPathLen = 0;
    int _demoPathIdx = 0;               // how far along the route it's got

    void renderScoresScreen(GFXcanvas16 &canvas) {
        canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
        hiscore::drawTable(canvas, _scores.table(), "HIGH SCORES", 30);
        if (millis() % 1000 < 600) hiscore::printCentred(canvas, "HIT BUTTON TO START", 130, ArcadeConfig::COLOR_WHITE);
    }

    void initLevel() {
        _lander.spawn();
        _padX = random(15, ArcadeConfig::PORTRAIT_WIDTH - 15 - _padWidth);

        int gravityIncrements = (_level - 1) / 3;
        _currentGravity = 0.025f + (gravityIncrements * 0.005f);
        if (_currentGravity > 0.10f) _currentGravity = 0.10f;

        _obstacles.generateNewMap(_level, _padX, _padWidth);

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
        canvas.print("BEST: ");
        canvas.setTextColor(ArcadeConfig::COLOR_GREEN);
        char hiBuf[24];
        canvas.print(_scores.bestLine(hiBuf, sizeof(hiBuf), ""));
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

        const int rank = _scores.lastRank();
        if (rank >= 0) {
            char buf[24];
            snprintf(buf, sizeof(buf), rank == 0 ? "NEW HIGH SCORE!" : "HIGH SCORE #%d", rank + 1);
            hiscore::printCentred(canvas, buf, 92, ArcadeConfig::COLOR_YELLOW);
        }

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

    // Is the straight line from (x0,y0) to (x1,y1) clear of the rocks?
    bool demoLineClear(float x0, float y0, float x1, float y1, int margin) const {
        float dx = x1 - x0, dy = y1 - y0;
        int n = (int)(sqrtf(dx * dx + dy * dy) / 2.0f) + 1;
        for (int k = 0; k <= n; k++) {
            float t = (float)k / (float)n;
            if (!_obstacles.clearOf(x0 + dx * t, y0 + dy * t, 4, margin)) return false;
        }
        return true;
    }

    // Plans a route from the spawn point to just above the pad, on a 4px
    // grid, through cells clear of every rock by a margin (breadth-first, so
    // the fewest cells). The new rock placement always leaves such a way.
    void demoPlanRoute() {
        static int16_t prev[DEMO_GW * DEMO_GH];
        static int16_t queue[DEMO_GW * DEMO_GH];
        const int floorY = ArcadeConfig::PORTRAIT_HEIGHT - 10;
        auto freeCell = [&](int cx, int cy) {
            float x = cx * DEMO_CELL + DEMO_CELL / 2, y = cy * DEMO_CELL + DEMO_CELL / 2;
            if (x < 6 || x > ArcadeConfig::PORTRAIT_WIDTH - 6 || y < 6 || y > floorY - 12) return false;
            return _obstacles.clearOf(x, y, 4, 5);
        };
        for (auto &p : prev) p = -2;
        const int sx = (int)_lander.x / DEMO_CELL, sy = (int)_lander.y / DEMO_CELL;
        const int goalY = (floorY - 24) / DEMO_CELL;
        const int padL = (_padX + 6) / DEMO_CELL, padR = (_padX + _padWidth - 6) / DEMO_CELL;
        int head = 0, tail = 0, goal = -1;
        const int start = sy * DEMO_GW + sx;
        prev[start] = -1;
        queue[tail++] = (int16_t)start;
        while (head < tail && goal < 0) {
            int c = queue[head++], cx = c % DEMO_GW, cy = c / DEMO_GW;
            if (cy >= goalY && cx >= padL && cx <= padR) { goal = c; break; }
            for (int d = 0; d < 8; d++) {
                static const int8_t DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
                static const int8_t DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
                int nx = cx + DX[d], ny = cy + DY[d];
                if (nx < 0 || ny < 0 || nx >= DEMO_GW || ny >= DEMO_GH) continue;
                int n = ny * DEMO_GW + nx;
                if (prev[n] != -2 || !freeCell(nx, ny)) continue;
                prev[n] = (int16_t)c;
                queue[tail++] = (int16_t)n;
            }
        }
        // Walk it back (goal to start), then reverse. No route: straight for the pad.
        int len = 0;
        for (int c = goal; c >= 0 && len < DEMO_PATH_MAX; c = prev[c]) {
            _demoPathX[len] = (int16_t)((c % DEMO_GW) * DEMO_CELL + DEMO_CELL / 2);
            _demoPathY[len] = (int16_t)((c / DEMO_GW) * DEMO_CELL + DEMO_CELL / 2);
            len++;
        }
        for (int i = 0; i < len / 2; i++) {
            int16_t t = _demoPathX[i]; _demoPathX[i] = _demoPathX[len - 1 - i]; _demoPathX[len - 1 - i] = t;
            t = _demoPathY[i]; _demoPathY[i] = _demoPathY[len - 1 - i]; _demoPathY[len - 1 - i] = t;
        }
        // It ends over the pad's centre (not just its last grid cell), or,
        // with no route, that's the whole route.
        if (len == 0 || len == DEMO_PATH_MAX) len = len ? len - 1 : 0;
        _demoPathX[len] = (int16_t)(_padX + _padWidth / 2);
        _demoPathY[len] = (int16_t)(floorY - 24);
        len++;
        _demoPathLen = len;
        _demoPathIdx = 0;
    }

    // The autopilot, as the game's own inputs: a stick angle (the raw
    // 0-4095 the game maps to -45..45 degrees) and the thrust button. Heads
    // for the furthest route point in clear sight, at a capped speed; over
    // the pad it comes straight down at _demoDescent. The wanted change of
    // velocity becomes a thrust direction, and thrust is pulsed so its
    // average matches the force wanted.
    void demoPilot(bool &btnA, int &joyX) {
        const float x = _lander.x, y = _lander.y;
        const float padC = _padX + _padWidth / 2.0f;
        // Pure pursuit: from the route point nearest the ship (never going
        // back along it), aim a few points further on. The route keeps a
        // margin from the rocks, so staying close to it is safe; aiming at
        // a far point instead lets the lander's momentum swing it wide.
        int best = _demoPathIdx;
        float bestD = 1e9f;
        for (int k = _demoPathIdx; k < _demoPathLen && k < _demoPathIdx + 12; k++) {
            float dx = _demoPathX[k] - x, dy = _demoPathY[k] - y, d = dx * dx + dy * dy;
            if (d < bestD) { bestD = d; best = k; }
        }
        _demoPathIdx = best;
        const int aim = min(best + 3, _demoPathLen - 1);
        const float tx = _demoPathX[aim], ty = _demoPathY[aim];
        // Fuel is the constraint (a landing needs some left), so it lets
        // gravity do the work: falls fast while well clear of the ground and
        // brakes late, as a player would, rather than hovering all the way.
        const float padTop = ArcadeConfig::PORTRAIT_HEIGHT - 14.0f;
        const float fall = (padTop - y > 30.0f) ? 1.3f : _demoDescent;
        float vxWant, vyWant;
        // Anywhere over the pad (a few px in from its ends) with a clear
        // drop below counts: it then centres up on the way down.
        const bool overPad = x >= _padX + 4.0f && x <= _padX + _padWidth - 4.0f &&
                             demoLineClear(x, y, x, padTop, 3);
        if (overPad) {
            vxWant = (padC - x) * 0.05f;
            vyWant = fall;
        } else {
            float dx = tx - x, dy = ty - y;
            // Along the route at a steady pace, faster only when it's a drop.
            float d = sqrtf(dx * dx + dy * dy) + 0.001f;
            float v = dy > d * 0.8f ? min(fall, 0.9f) : 0.6f;
            vxWant = dx / d * v;
            vyWant = dy / d * v;
        }
        // Wanted acceleration, then the thrust that gives it against gravity.
        float ax = constrain((vxWant - _lander.vx) * 0.2f, -0.1f, 0.1f);
        float ay = constrain((vyWant - _lander.vy) * 0.2f, -0.1f, 0.1f);
        float fx = ax, fy = ay - _currentGravity;          // thrust must supply this
        float angle = atan2f(fx, -fy);                     // 0 = straight up
        angle = constrain(angle, -PI / 4.0f, PI / 4.0f);
        // Thrust is all or nothing, so pulse it: bank the fraction of a full
        // burn wanted each step and fire once a whole one's due.
        float need = fy < 0.0f ? sqrtf(fx * fx + fy * fy) : 0.0f;
        _demoThrustAcc += min(need / THRUST_POWER, 1.0f);
        btnA = _demoThrustAcc >= 1.0f && _lander.fuel > 0.0f;
        if (btnA) _demoThrustAcc -= 1.0f;
        joyX = (int)((angle / (PI / 4.0f) + 1.0f) * 0.5f * 4095.0f);
    }

    void startDemo() {
        _demo = true;
        _demoLandings = 0;
        _demoDescent = 0.6f;
        _score = 0;
        _level = (int)random(1, 8);
        _lander.resetPools();
        _particles.clearAll();
        initLevel();
        demoPlanRoute();
        _isTitleScreen = false;
        _isGameOver = false;
        _demoUntil = millis() + (unsigned long)random((long)DEMO_MIN_MS, (long)DEMO_MAX_MS + 1);
    }

    // Back to the title, leaving nothing of the demo behind.
    void endDemo() {
        _demo = false;
        _score = 0;
        _level = 1;
        _lander.resetPools();
        _particles.clearAll();
        initLevel();
        _isTitleScreen = true;
        _isGameOver = false;
        _titleAWasHeld = false;
        _attractModeTimer = millis();
        _showInstructionPage = _showScoresPage = false;
    }

    void startGame(AudioEngine &audio) {
        _score = 0; _level = 1;
        _lander.resetPools();
        _particles.clearAll();
        initLevel();
        _isTitleScreen = false;
        _isGameOver    = false;
        audio.playLaunchMelody();
        // Music plays during a game only (through the between-level
        // screens), not on the title screen; it stops at game over.
        audio.loopWAV("/audio/flux-lander.wav");
    }

    void drawDemoOverlay(GFXcanvas16 &canvas) {
        canvas.setTextSize(1);
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.setCursor(4, 14);
        canvas.print("DEMO");
        if ((millis() / 500) % 2 == 0) {
            canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
            canvas.setCursor(ArcadeConfig::PORTRAIT_WIDTH / 2 - 39, 60);
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

public:
    GameEngineLander() {}

    void setTFT(Adafruit_ST7735 &tft) { _tft = &tft; }

    void init(AudioEngine &audio) {
        _scores.begin("lander");
        _highScore = (int)_scores.best();
        _naming = false;

        _score               = 0;
        _level               = 1;
        _lander.resetPools();
        _particles.clearAll();
        _isTitleScreen       = true;
        _isGameOver          = false;
        _attractModeTimer    = millis();
        _titleAWasHeld       = true;
        _showInstructionPage = _showScoresPage = false;
        _btnBWasHeld         = true;
        _demo                = false;
        initLevel();
        audio.playLanderStartSound();
        audio.preload("/audio/pickup.wav");     // the fuel pickup; loaded now, not on the first
    }

    // `input` is for the name entry (the rest of the game reads the raw values).
    bool update(GFXcanvas16 &canvas, bool btnA, bool btnB,
                int joyX, int joyY, AudioEngine &audio, const InputState &input) {

        // Button B: require release first, then hold 2s to exit — the same
        // convention Asteroid Flux, Platform Flux and Tank Flux use. A bare
        // press exited instantly, so brushing the button lost a run.
        if (_naming) {
            _btnBHoldStart = 0;            // B steps back a letter there
        } else if (_btnBWasHeld) {
            if (!btnB) _btnBWasHeld = false;
        } else if (btnB) {
            if (_btnBHoldStart == 0) _btnBHoldStart = millis();
            if (millis() - _btnBHoldStart > EXIT_HOLD_MS) {
                _btnBHoldStart = 0;
                // Quitting mid-game: the score still goes on the table,
                // under the last name entered.
                if (!_demo && !_isTitleScreen && _lander.lives > 0) _scores.record(_score);
                audio.mute();
                return false;
            }
        } else {
            _btnBHoldStart = 0;
        }

        // ---- ATTRACT DEMO: A plays for real, B back to the title ----
        const bool aPressed = btnA && !_prevBtnA, bPressed = btnB && !_prevBtnB;
        _prevBtnA = btnA;
        _prevBtnB = btnB;
        if (_demo) {
            if (aPressed) { endDemo(); _titleAWasHeld = true; startGame(audio); return true; }
            if (bPressed) { endDemo(); return true; }
            if (millis() >= _demoUntil && !_lander.isDisintegrating) { endDemo(); return true; }
            demoPilot(btnA, joyX);
        }
        Silence silence(audio, _demo);

        // ---- NAME ENTRY: then the game-over screen ----
        if (_naming) {
            canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
            _scores.draw(canvas);
            if (_tft) _tft->drawRGBBitmap(0, 0, canvas.getBuffer(),
                ArcadeConfig::PORTRAIT_WIDTH, ArcadeConfig::PORTRAIT_HEIGHT);
            if (_scores.update(input, 2)) {
                _naming            = false;
                _highScore         = (int)_scores.best();
                _isGameOver        = true;
                _gameOverEnteredMs = millis();
                _endInputArmed     = false;
                _btnBWasHeld       = true;   // B from the entry isn't the start of a quit
            }
            return true;
        }

        // ---- ATTRACT / TITLE SCREEN ----
        if (_isTitleScreen) {
            // Title, how-to-fly, high scores (8 seconds each), then the demo.
            if (millis() - _attractModeTimer > 8000UL) {
                if (!_showScoresPage) {
                    if (_showInstructionPage) _showScoresPage = true;
                    _showInstructionPage = true;
                    _attractModeTimer    = millis();
                } else {
                    startDemo();
                    return true;
                }
            }

            // Draw to canvas — single blit at bottom handles flush
            if (!_showInstructionPage) renderTitleScreen(canvas);
            else if (!_showScoresPage) renderInstructionScreen(canvas);
            else                       renderScoresScreen(canvas);

            if (_titleAWasHeld) {
                if (!btnA) _titleAWasHeld = false;
            } else if (btnA) {
                startGame(audio);
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
            if (_demo) {
                // The demo lands, shows it, then flies the next level; from
                // the second landing on, one approach in four comes in too
                // fast, so a demo sometimes shows a crash too.
                drawDemoOverlay(canvas);
                if (_tft) _tft->drawRGBBitmap(0, 0, canvas.getBuffer(),
                    ArcadeConfig::PORTRAIT_WIDTH, ArcadeConfig::PORTRAIT_HEIGHT);
                if (millis() - _gameOverEnteredMs > DEMO_LANDED_MS) {
                    _demoLandings++;
                    _demoDescent = random(0, 4) == 0 ? 1.5f : 0.6f;
                    initLevel();
                    demoPlanRoute();
                    _isGameOver = false;
                }
                return true;
            }
            if (_tft) _tft->drawRGBBitmap(0, 0, canvas.getBuffer(),
                ArcadeConfig::PORTRAIT_WIDTH, ArcadeConfig::PORTRAIT_HEIGHT);

            if (!_endInputArmed && !btnA &&
                millis() - _gameOverEnteredMs >= ArcadeConfig::GAMEOVER_INPUT_DELAY_MS) {
                _endInputArmed = true;
            }
            if (_endInputArmed && btnA) {
                if (_lander.lives > 0) {
                    initLevel();
                } else {
                    _lander.resetPools();
                    _particles.clearAll();
                    initLevel();
                    _isTitleScreen    = true;
                    _titleAWasHeld    = true;
                    _attractModeTimer = millis();
                    _showInstructionPage = _showScoresPage = false;
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
                _showInstructionPage = _showScoresPage = false;
                audio.stopLoop();   // timed out on a between-level screen
                audio.playLanderStartSound();
            }
            return true;
        }

        // ---- PHYSICS: fixed 50Hz steps ----
        // Exactly one step per PHYSICS_TICK_MS of real time, whatever the
        // frame rate: the clock advances by the step, not to "now", so a
        // 60fps frame loop still gets 50 steps a second (resetting to "now"
        // gave a step only every other 16.7ms frame, 30 a second, and the
        // whole game slowed and changed with the frame rate). A slow frame
        // catches up, at most MAX_STEPS at once; beyond that time is dropped.
        unsigned long now = millis();
        static const int MAX_STEPS = 3;
        int steps = 0;
        while (now - _lastPhysicsTick >= PHYSICS_TICK_MS && steps < MAX_STEPS) {
            _lastPhysicsTick += PHYSICS_TICK_MS;
            ++steps;
        }
        if (now - _lastPhysicsTick >= PHYSICS_TICK_MS) _lastPhysicsTick = now;
        const bool physicsTick = steps > 0;

        if (_lander.isDisintegrating) {
            if (physicsTick) _particles.update();
            if (now - _lander.explosionStartTime > 1200 && _demo) {
                endDemo();                       // a demo crash just ends the demo
                return true;
            }
            if (now - _lander.explosionStartTime > 1200) {
                if (_lander.lives > 0) {
                    initLevel();
                } else {
                    audio.stopLoop();
                    audio.playGameOverSound(gameend_data, sizeof(gameend_data));
                    // A name for the table first, if the score made it.
                    _scores.forget();
                    if (_scores.offer(_score)) {
                        _naming = true;
                        return true;
                    }
                    _isGameOver        = true;
                    _gameOverEnteredMs = now;
                    _endInputArmed     = false;
                }
            }
        } else {
          for (int step = 0; step < steps && !_lander.isDisintegrating && !_isGameOver; ++step) {
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
                    audio.playWAV("/audio/pickup.wav");
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
                    _level++;
                    _isGameOver        = true;
                    _gameOverEnteredMs = now;
                    _endInputArmed     = false;
                    audio.playLandingSuccessSound();
                } else {
                    _lander.kill(_particles);
                    audio.playExplosionSound(explosion_data, sizeof(explosion_data));
                }
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
        if (_demo) drawDemoOverlay(canvas);

        if (_tft) _tft->drawRGBBitmap(0, 0, canvas.getBuffer(),
            ArcadeConfig::PORTRAIT_WIDTH, ArcadeConfig::PORTRAIT_HEIGHT);

        return true;
    }
};

#endif // GAME_ENGINE_LANDER_H