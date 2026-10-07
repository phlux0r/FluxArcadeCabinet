#ifndef PLATFORM_FLUX_GAME_H
#define PLATFORM_FLUX_GAME_H

#include "../../games/IGame.h"
#include "../../cabinet/ArcadeConfig.h"
#include "../../cabinet/ParticleManager.h"
#include "../../cabinet/AudioEngine.h"
#include "../../cabinet/HighScores.h"

#include "PlayerRunner.h"
#include "PlatformManager.h"
#include "FlyingEnemyManager.h"
#include "RunnerPowerUpManager.h"
#include "LevitationPowerUpManager.h"
#include "RollingBoulderManager.h"
#include "RunnerBackdrop.h"
#include "RunnerBoss.h"
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
    RunnerBackdrop        _backdrop;
    RunnerBoss            _boss;

    // "+10" popups where a hazard was got past: they ride the scroll and rise.
    struct Popup { float x, y; int points; unsigned long at; bool active; };
    static const int POPUPS = 4;
    Popup _popups[POPUPS] = {};

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

    // NAME: entering a name for the high-score table, after the last life.
    // PICK: the stage select.
    enum GamePhase { PHASE_ATTRACT, PHASE_PLAYING, PHASE_DEATH, PHASE_NAME, PHASE_GAMEOVER, PHASE_PICK };
    GamePhase _phase = PHASE_ATTRACT;

    // Title, how-to-play, then the autopilot demo (a run with _demo set).
    enum AttractSlide { SLIDE_SPLASH, SLIDE_INFO, SLIDE_SCORES };
    bool _demo = false;
    unsigned long _demoUntil = 0;
    AttractSlide  _attractSlide      = SLIDE_SPLASH;
    unsigned long _attractSlideTimer = 0;

    // The stage select (a test cheat): B held and A on an attract screen.
    // A test run plays from _testFrom; nothing from it goes on the table.
    static const int PICK_STAGES = 4 * PlatformManager::TIERS_PER_LOOP;   // four loops
    static const unsigned long PICK_TIMEOUT_MS = 20000, PICK_REPEAT_DELAY_MS = 400, PICK_REPEAT_MS = 150;
    bool _test = false;
    int  _testFrom = 1;
    int  _pick = 1, _pickDir = 0;
    unsigned long _pickRepeatAt = 0, _pickAt = 0;

    unsigned long _phaseTimer = 0;
    bool _uiDirty = true;

    unsigned long _gameOverEnteredMs = 0;
    static const unsigned long GAMEOVER_TIMEOUT_MS = 30000UL;

    // The cabinet's table for this game; _highScore is its top score.
    hiscore::ScoreBoard _scores;

    // Sounds: each one's own WAV if it's on the card, else a shared WAV
    // if it has one and that's there, else a tone or melody (what Runner
    // played before it had optional sounds). Checked once, in init().
    enum Sfx { SFX_TITLE, SFX_JUMP, SFX_POINTS, SFX_STAR, SFX_FLY, SFX_ROCK, SFX_BOULDER,
               SFX_STAGE, SFX_LIFE, SFX_DEATH, SFX_OVER, SFX_DUCK, SFX_DART, SFX_JET, SFX_BOSS, SFX_DROP, SFX_COUNT };
    struct SfxDef { const char *path, *fallback; int hz, ms; const int *notes, *durs; int len; };
    static constexpr int TITLE_N[] = { 523, 659, 784, 1047 }, TITLE_D[] = { 80, 80, 80, 150 };
    static constexpr int JUMP_N[]  = { 700, 1050 },           JUMP_D[]  = { 35, 45 };
    static constexpr int STAGE_N[] = { 784, 988, 1175 },      STAGE_D[] = { 60, 60, 140 };
    static constexpr int LIFE_N[]  = { 880, 1175, 1568, 2093 }, LIFE_D[] = { 70, 70, 70, 200 };
    static constexpr int DEATH_N[] = { 500, 350, 220 },       DEATH_D[] = { 100, 100, 200 };
    static constexpr int OVER_N[]  = { 392, 330, 262, 196 },  OVER_D[]  = { 150, 150, 150, 350 };
    static constexpr int BOSS_N[]  = { 220, 0, 220, 0, 165 }, BOSS_D[]  = { 120, 60, 120, 60, 300 };
    static constexpr SfxDef SFX[SFX_COUNT] = {
        { "/audio/runner_title.wav",   "/audio/lander_start.wav", 0, 0, TITLE_N, TITLE_D, 4 },  // SFX_TITLE
        { "/audio/jump.wav",           nullptr, 0, 0, JUMP_N, JUMP_D, 2 },                       // SFX_JUMP
        { "/audio/runner_points.wav",  nullptr, 1500, 20, nullptr, nullptr, 0 },                 // SFX_POINTS
        { "/audio/runner_star.wav",    "/audio/powerup.wav", 1000, 80, nullptr, nullptr, 0 },    // SFX_STAR
        { "/audio/runner_fly.wav",     "/audio/powerup.wav", 1400, 100, nullptr, nullptr, 0 },   // SFX_FLY
        { "/audio/runner_rock.wav",    nullptr, 300, 60, nullptr, nullptr, 0 },                  // SFX_ROCK
        { "/audio/runner_boulder.wav", nullptr, 220, 80, nullptr, nullptr, 0 },                  // SFX_BOULDER
        { "/audio/runner_stage.wav",   nullptr, 0, 0, STAGE_N, STAGE_D, 3 },                     // SFX_STAGE
        { "/audio/runner_life.wav",    nullptr, 0, 0, LIFE_N, LIFE_D, 4 },                       // SFX_LIFE
        { "/audio/death.wav",          nullptr, 0, 0, DEATH_N, DEATH_D, 3 },                     // SFX_DEATH
        { "/audio/gameend.wav",        nullptr, 0, 0, OVER_N, OVER_D, 4 },                       // SFX_OVER
        { "/audio/runner_duck.wav",    nullptr, 0, 0, nullptr, nullptr, 0 },                     // SFX_DUCK (card only)
        { "/audio/runner_dart.wav",    nullptr, 1800, 30, nullptr, nullptr, 0 },                 // SFX_DART
        { "/audio/runner_jet.wav",     nullptr, 160, 90, nullptr, nullptr, 0 },                  // SFX_JET
        { "/audio/runner_boss.wav",    nullptr, 0, 0, BOSS_N, BOSS_D, 5 },                       // SFX_BOSS
        { "/audio/runner_drop.wav",    nullptr, 260, 50, nullptr, nullptr, 0 },                  // SFX_DROP
    };
    static constexpr const char* MUSIC = "/audio/flux-runner.wav";
    AudioEngine* _audio = nullptr;
    bool _sfxOnCard[SFX_COUNT] = {}, _fallbackOnCard[SFX_COUNT] = {}, _musicOnCard = false;
    const char* _lastSfx = "";           // what the last sfx() played, for the harness
    unsigned _sfxPlayed = 0;             // a bit per Sfx asked for, for the harness

    // A missing file would otherwise cost an SD open every time it's asked
    // for; what will play is decoded into the mixer's cache now, so the
    // first jump isn't late.
    void findSounds(AudioEngine &audio) {
        for (int i = 0; i < SFX_COUNT; ++i) {
            _sfxOnCard[i] = audio.exists(SFX[i].path);
            _fallbackOnCard[i] = !_sfxOnCard[i] && SFX[i].fallback && audio.exists(SFX[i].fallback);
            if (_sfxOnCard[i]) audio.preload(SFX[i].path);
            else if (_fallbackOnCard[i]) audio.preload(SFX[i].fallback);
        }
        _musicOnCard = audio.exists(MUSIC);
    }

    // Nothing plays in a demo (the Silence guard drops it anyway).
    void sfx(Sfx s) {
        if (!_audio || _demo) return;
        const SfxDef &d = SFX[s];
        _sfxPlayed |= 1u << s;
        if (_sfxOnCard[s])           { _audio->playWAV(d.path); _lastSfx = d.path; }
        else if (_fallbackOnCard[s]) { _audio->playWAV(d.fallback); _lastSfx = d.fallback; }
        else if (d.notes)            { _audio->playMelody(d.notes, d.durs, d.len); _lastSfx = "melody"; }
        else if (d.hz)               { _audio->playTone(d.hz, d.ms); _lastSfx = "tone"; }
        else                         _lastSfx = "none";
    }

    void loadHighScore() {
        _scores.begin("runner");
        _highScore = (int)_scores.best();
    }

    void renderScoresScreen(GFXcanvas16 &canvas) {
        canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
        hiscore::drawTable(canvas, _scores.table(), "HIGH SCORES", 18);
        if ((millis() / 500) & 1) hiscore::printCentred(canvas, "[BTN A] TO START", 108, ArcadeConfig::COLOR_CYAN);
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

        int sx = 58;
        if (_test) {
            canvas.setTextColor(ArcadeConfig::COLOR_ORANGE);
            canvas.setCursor(sx, 1); canvas.print("T");
            sx += 6;
        }
        canvas.setTextColor(ArcadeConfig::COLOR_YELLOW);
        canvas.setCursor(sx, 1);
        canvas.print(_score);

        canvas.setTextColor(ArcadeConfig::COLOR_GREY);
        canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH - 54, 1);
        canvas.print("HI:"); canvas.print(_highScore);
    }

    // Points for every hazard got past this frame, each with its popup.
    void scoreClearedHazards() {
        const float px = _player.getX();
        float x, y;
        int pts;
        while ((pts = _platforms.takeCleared(px, x, y)) > 0 || (pts = _boulders.takeCleared(px, x, y)) > 0 ||
               (pts = _boss.takeCleared(px, x, y)) > 0) {
            _score += pts;
            addPopup(x, y, pts);
            sfx(SFX_POINTS);
        }
    }

    void addPopup(float x, float y, int points) {
        Popup* slot = &_popups[0];
        for (auto &p : _popups) {
            if (!p.active) { slot = &p; break; }
            if (p.at < slot->at) slot = &p;   // all busy: the oldest goes
        }
        *slot = Popup{ x, y, points, millis(), true };
    }

    void updatePopups(float scrollSpeed) {
        for (auto &p : _popups) {
            if (!p.active) continue;
            p.x -= scrollSpeed;
            if (millis() - p.at >= ArcadeConfig::RUNNER_POPUP_MS) p.active = false;
        }
    }

    void drawPopups(GFXcanvas16 &canvas) {
        canvas.setTextSize(1);
        for (const auto &p : _popups) {
            if (!p.active) continue;
            const unsigned long age = millis() - p.at;
            if (age > ArcadeConfig::RUNNER_POPUP_MS - 200 && ((age / 60) & 1)) continue;   // blinks out
            char buf[8];
            snprintf(buf, sizeof(buf), "+%d", p.points);
            const int w = (int)strlen(buf) * 6;
            const int y = (int)p.y - (int)(age * 10 / ArcadeConfig::RUNNER_POPUP_MS);
            canvas.setTextColor(ArcadeConfig::COLOR_YELLOW);
            canvas.setCursor((int)p.x - w / 2, max(y, ArcadeConfig::UI_MARGIN_TOP + 1));
            canvas.print(buf);
        }
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

        // The art's bottom 16px is a dark strip reserved for text: the best
        // score over the start prompt, each centred (side by side, a long
        // best ran into the prompt).
        int stripY = ArcadeConfig::LANDSCAPE_HEIGHT - 16;
        char hiBuf[24];
        hiscore::printCentred(canvas, _scores.bestLine(hiBuf, sizeof(hiBuf), "BEST: "), stripY, ArcadeConfig::COLOR_CYAN);
        hiscore::printCentred(canvas, "[BTN A] START", stripY + 8, ArcadeConfig::COLOR_CYAN);
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
        canvas.setCursor(6, 22);
        canvas.print("[A] JUMP   [B] DUCK");

        // The hazards, no heading: a line each.
        canvas.setTextColor(ArcadeConfig::COLOR_ORANGE);
        canvas.setCursor(10, 33);
        canvas.print("FIRE PITS");
        canvas.setTextColor(ArcadeConfig::COLOR_GREEN);
        canvas.setCursor(10, 42);
        canvas.print("PLATFORMS & GAPS");
        canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
        canvas.setCursor(10, 51);
        canvas.print("MOVING PLATFORMS");
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.setCursor(10, 60);
        canvas.print("STAIRS & SPIKE TRAPS");
        canvas.setTextColor(ArcadeConfig::COLOR_AMBER);
        canvas.setCursor(10, 69);
        canvas.print("ROLLING BOULDERS");
        canvas.setTextColor(ArcadeConfig::COLOR_MAGENTA);
        canvas.setCursor(10, 78);
        canvas.print("FLYING ENEMY + ROCKS");
        canvas.setTextColor(ArcadeConfig::COLOR_ION_BLUE);
        canvas.setCursor(10, 87);
        canvas.print("NIGHT: DUCK UNDER");
        canvas.setCursor(10, 95);
        canvas.print(" BEAMS, JETS, DARTS");

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

    // A game from stage `from`; a test run (the stage select) if test.
    void startNewGame(AudioEngine &audio, int from = 1, bool test = false) {
        // Cut off whatever's still playing (e.g. the game-over sound, if
        // "play again" was pressed before it finished) so it doesn't keep
        // running into the new game.
        audio.mute();
        _test = test;
        _testFrom = from;
        _score = 0;
        _lives = ArcadeConfig::RUNNER_LIVES;
        startStage(from);
        _loopsSeen = _platforms.loopsCompleted();   // no extra life for the loop it starts in
        // Music plays during a game only, not on the attract screen; it
        // carries on through lost lives and stops at game over.
        if (_musicOnCard) audio.loopWAV(MUSIC);
    }

    // (Re)starts the world at `stage`: its terrain, hazards and speed, on a
    // safe ledge. Used for a new game and after each lost life.
    void startStage(int stage) {
        _stage = stage;
        _diedThisStage = false;
        _jumpPressedAt = 0;
        _particles.clearAll();
        for (auto &p : _popups) p.active = false;
        _platforms.initGame(stage);
        _enemies.initGame();
        _boulders.initGame();
        _boss.reset();
        if (PlatformManager::isBossStage(stage)) _boss.start(_platforms.getLoop());
        _powerUp.reset();
        _levitationPowerUp.reset();
        _playerXOffset = 0.0f;
        _player.reset((float)ArcadeConfig::RUNNER_BASE_X, ArcadeConfig::LANDSCAPE_HEIGHT - 8);
        _uiDirty = true;
        _phase = PHASE_PLAYING;
        _phaseTimer = millis();
        char title[20];
        snprintf(title, sizeof(title), "STAGE %d", stage);
        if (PlatformManager::isBossStage(stage)) showBanner(title, "BOSS: SURVIVE IT", ArcadeConfig::COLOR_ORANGE);
        else showBanner(title, PlatformManager::worldName(_platforms.getLoop()), ArcadeConfig::COLOR_CYAN);
    }

    // Called each frame: a new stage reached means the last one was cleared.
    void checkStageProgress() {
        int now = _platforms.stageNumber();
        if (now <= _stage) return;
        bool perfect = !_diedThisStage;
        int bonus = ArcadeConfig::RUNNER_STAGE_BONUS * (perfect ? 2 : 1);
        if (PlatformManager::isBossStage(_stage)) bonus += ArcadeConfig::RUNNER_BOSS_POINTS;   // the boss survived
        _score += bonus;
        _stage = now;
        _diedThisStage = false;
        _uiDirty = true;

        char title[20], sub[24];
        // A new loop's first stage names its world; a boss stage says so.
        _boss.reset();
        if ((_stage - 1) % PlatformManager::TIERS_PER_LOOP == 0)
            snprintf(title, sizeof(title), "STAGE %d: %s", _stage, PlatformManager::worldName(_platforms.getLoop()));
        else if (PlatformManager::isBossStage(_stage))
            snprintf(title, sizeof(title), "STAGE %d: BOSS", _stage);
        else
            snprintf(title, sizeof(title), "STAGE %d", _stage);
        if (PlatformManager::isBossStage(_stage)) {
            _boss.start(_platforms.getLoop());
            sfx(SFX_BOSS);
        }
        snprintf(sub, sizeof(sub), perfect ? "PERFECT +%d" : "CLEAR +%d", bonus);
        showBanner(title, sub, perfect ? ArcadeConfig::COLOR_GREEN : ArcadeConfig::COLOR_WHITE);

        // A whole loop of stages done: an extra life.
        if (_platforms.loopsCompleted() > _loopsSeen) {
            _loopsSeen = _platforms.loopsCompleted();
            if (_lives < ArcadeConfig::RUNNER_MAX_LIVES) {
                _lives++;
                snprintf(sub, sizeof(sub), "EXTRA LIFE +%d", bonus);
                showBanner(title, sub, ArcadeConfig::COLOR_ORANGE);
                sfx(SFX_LIFE);
                return;
            }
        }
        sfx(SFX_STAGE);
    }

    // How far forward the runner may go: further in a boss stretch; past
    // it (just after one), back in a step a frame rather than a jump.
    float offsetCeiling(float offset) const {
        const float hi = _platforms.getTier() == PlatformManager::BOSS_TIER
                             ? (float)ArcadeConfig::RUNNER_BOSS_MAX_OFFSET : (float)ArcadeConfig::RUNNER_X_MAX_OFFSET;
        return max(hi, offset - ArcadeConfig::RUNNER_X_MOVE_SPEED);
    }

    // At Night, how far ahead the runner's lantern lights fully (screen x).
    int lanternReach() const {
        if (!PlatformManager::isNight(_platforms.getLoop())) return 9999;
        return (int)_player.getX() + RUNNER_WIDTH + ArcadeConfig::NIGHT_LANTERN;
    }

    // Everything but the runner, from the backdrop up. A lost life draws
    // it stopped, under the burst.
    void renderWorld(GFXcanvas16 &canvas) {
        _backdrop.render(canvas, 11, _platforms.getLoop());
        _platforms.render(canvas, lanternReach());
        _particles.render(canvas, 11);
        _powerUp.render(canvas);
        _levitationPowerUp.render(canvas);
        _enemies.render(canvas, _platforms.getLoop());
        _boulders.render(canvas, _platforms, _platforms.getLoop());
        _boss.render(canvas);
    }

    void triggerPlayerDeath() {
        _particles.triggerExplosion(_player.getX() + RUNNER_WIDTH / 2.0f,
                                     _player.getY() + RUNNER_HEIGHT / 2.0f, 60, 3);   // trailed
        sfx(SFX_DEATH);
    }

    // ---- Stage select --------------------------------------------------------

    void enterPicker() {
        _phase = PHASE_PICK;
        _pick = 1; _pickDir = 0;
        _pickAt = millis();
        previewStage();
    }

    // The picked stage's world, scrolled on about a screen past its safe
    // starting ledge so its own terrain shows.
    void previewStage() {
        _platforms.initGame(_pick);
        for (float moved = 0; moved < ArcadeConfig::LANDSCAPE_WIDTH; moved += _platforms.getScrollSpeed())
            _platforms.update();
    }

    // What each stage of a loop brings (see PlatformManager's tiers, and
    // Night's obstacles on top).
    static const char* stageHazards(int stage) {
        static const char* const what[PlatformManager::TIERS_PER_LOOP] = {
            "FIRE PITS", "FIRE PITS, STAIRS", "PLATFORMS", "MOVING PLATFORMS",
            "PLATFORMS + SHIP", "STAIRS, SPIKES", "SPIKES, BOULDERS", "ALL + SHIPS", "BOSS SHIP",
        };
        static const char* const night[PlatformManager::TIERS_PER_LOOP] = {
            "PITS, BEAMS", "PITS, STAIRS, BEAMS", "PLATFORMS", "MOVING PLATFORMS",
            "PLATFORMS + SHIP", "SPIKES, FLAME JETS", "BOULDERS, DARTS", "ALL + SHIPS", "BOSS SHIP + DARTS",
        };
        const int tier = (stage - 1) % PlatformManager::TIERS_PER_LOOP;
        return PlatformManager::isNight((stage - 1) / PlatformManager::TIERS_PER_LOOP) ? night[tier] : what[tier];
    }

    // The stage's terrain as it starts, behind its number and hazards.
    void renderPicker(GFXcanvas16 &canvas) {
        _backdrop.render(canvas, 0, _platforms.getLoop());
        _platforms.render(canvas, PlatformManager::isNight(_platforms.getLoop())
                                      ? ArcadeConfig::RUNNER_BASE_X + RUNNER_WIDTH + ArcadeConfig::NIGHT_LANTERN : 9999);
        canvas.fillRect(16, 28, 128, 60, ArcadeConfig::COLOR_BLACK);
        canvas.drawRect(16, 28, 128, 60, ArcadeConfig::COLOR_ORANGE);
        char buf[24];
        snprintf(buf, sizeof(buf), "STAGE %d", _pick);
        hiscore::printCentred(canvas, buf, 33, ArcadeConfig::COLOR_WHITE, 2);
        snprintf(buf, sizeof(buf), "LOOP %d: %s", _platforms.getLoop() + 1, PlatformManager::worldName(_platforms.getLoop()));
        hiscore::printCentred(canvas, buf, 52, ArcadeConfig::COLOR_CYAN);
        hiscore::printCentred(canvas, stageHazards(_pick), 62, ArcadeConfig::COLOR_CYAN);
        hiscore::printCentred(canvas, "A: TEST RUN  B: BACK", 76, ArcadeConfig::COLOR_ORANGE);
        hiscore::printCentred(canvas, "STAGE SELECT", 2, ArcadeConfig::COLOR_ORANGE);
    }

    void leavePicker() {
        _phase = PHASE_ATTRACT;
        _attractSlide = SLIDE_SPLASH;
        _attractSlideTimer = millis();
    }

    // ---- Attract demo --------------------------------------------------------

    // The autopilot's stick: forward in the air (pushing forward through a
    // jump is what clears a wide fire pit, for the bot as for a player),
    // and on the ground towards `target`, an offset: normally right back,
    // keeping the most room to go forward, or wherever steps out from under
    // a falling rock or into a boss's gap.
    static float demoStick(bool onGround, float offset, float target) {
        return onGround ? constrain(target - offset, -1.0f, 1.0f) : 1.0f;
    }
    static constexpr float DEMO_BACK = (float)ArcadeConfig::RUNNER_X_MIN_OFFSET;

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
                      float target = DEMO_BACK) const {
        const float s = _platforms.getScrollSpeed();
        bool jumped = false;
        int landed = 0;
        for (int t = t0 + 1; t <= horizon; ++t) {
            if (--_demoBudget < 0) return false;
            const float stick = demoStick(st.onGround, st.off, target);   // read before this frame's jump, as in play
            if (t - 1 == jumpAt && (t == t0 + 1 ? st.canJump : st.onGround)) {
                st.vy = -ArcadeConfig::RUNNER_JUMP_VELOCITY;
                st.onGround = false;
                jumped = true;
            }
            st.off = constrain(st.off + stick * ArcadeConfig::RUNNER_X_MOVE_SPEED,
                               (float)ArcadeConfig::RUNNER_X_MIN_OFFSET, offsetCeiling(st.off));
            const float x = (float)ArcadeConfig::RUNNER_BASE_X + st.off;
            const float shift = s * (float)t;
            const float px = x + shift, pr = px + RUNNER_WIDTH, pb = st.y + RUNNER_HEIGHT;
            if (_platforms.firePitHitsPlayer(px, pr, pb)) return false;
            if (_platforms.spikeNear(px, pr, pb)) return false;
            // Night: it ducks on the ground under or near a column, or with
            // a dart coming (as demoPilot will), and every jet is taken as lit.
            const bool duck = st.onGround && _platforms.duckWanted(x, x + RUNNER_WIDTH, t);
            if (_platforms.nightHits(x, x + RUNNER_WIDTH, st.y + (duck ? RUNNER_DUCK_DROP : 0), pb, t)) return false;
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
            if (_boss.wouldHit(t, s, x, x + RUNNER_WIDTH, st.y, st.y + RUNNER_HEIGHT)) return false;   // after it moved, as in play
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

    bool demoSurvives(int jumpAt, int horizon, float target = DEMO_BACK) const {
        DemoState st{ _playerXOffset, _player.getY(), _player.getVy(),
                      _player.isOnGround(), _player.canJump() };
        return demoSurvives(st, 0, jumpAt, horizon, 1, target);
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
        in.joyY = demoStick(_player.isOnGround(), _playerXOffset, DEMO_BACK);
        in.btnB = _player.isOnGround() && _platforms.duckWanted(_player.getX(), _player.getX() + RUNNER_WIDTH);
        if (!_player.canJump() || demoSurvives(-1, H)) return in;
        // Somewhere else on the ground: right forward, or partway (a gap).
        if (_player.isOnGround()) {
            static const float targets[] = { (float)ArcadeConfig::RUNNER_X_MAX_OFFSET, 3.0f, -6.0f, 12.0f, -10.0f, 8.0f, 16.0f, -2.0f };
            static const float bossTargets[] = { 60.0f, 45.0f, 30.0f, 15.0f, 0.0f, 52.0f, 38.0f, 22.0f, 8.0f, -7.0f };
            const bool boss = _platforms.getTier() == PlatformManager::BOSS_TIER;
            const float* list = boss ? bossTargets : targets;
            const int n = boss ? (int)(sizeof(bossTargets) / sizeof(float)) : (int)(sizeof(targets) / sizeof(float));
            for (int k = 0; k < n; ++k) {
                const float t = list[k];
                if (demoSurvives(-1, H, t)) { in.joyY = demoStick(true, _playerXOffset, t); return in; }
            }
        }
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
        _test = false;
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
        _audio = &audio;
        loadHighScore();
        findSounds(audio);
        _phase              = PHASE_ATTRACT;
        _attractSlide       = SLIDE_SPLASH;
        _attractSlideTimer  = millis();
        _demo               = false;
        sfx(SFX_TITLE);   // the shared game-select jingle unless Runner has its own
    }

    void setTFT(Adafruit_ST7735 &tft) override { _tft = &tft; }
    bool flushesItself() const override { return true; }

    bool update(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) override {
        // Quitting is the cabinet's Back button (main.cpp, then onQuit()).

        // ---- ATTRACT DEMO: A plays for real, time's up ends it ----
        if (_demo) {
            if (input.btnAPressed) { endDemo(); startNewGame(audio); return true; }
            if (millis() >= _demoUntil && _phase == PHASE_PLAYING) { endDemo(); return true; }
        }
        Silence silence(audio, _demo);
        const InputState in = _demo ? demoPilot() : input;

        // ---- PHASE: ATTRACT ----
        if (_phase == PHASE_ATTRACT) {
            if (millis() - _attractSlideTimer > ArcadeConfig::ATTRACT_MODE_TIMER) {
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

            flushLandscape(canvas);

            // With B held, A opens the stage select.
            if (input.btnAPressed && input.btnB) enterPicker();
            else if (input.btnAPressed) startNewGame(audio);
            return true;
        }

        // ---- PHASE: STAGE SELECT — the stick steps the stage (left and
        // right by one, up and down by a loop), A starts a test run, B or
        // leaving it alone goes back ----
        if (_phase == PHASE_PICK) {
            if (input.btnAPressed) { startNewGame(audio, _pick, true); return true; }
            if (input.btnBPressed || millis() - _pickAt > PICK_TIMEOUT_MS) { leavePicker(); return true; }
            bool up, down, left, right;
            hiscore::screenDirs(input, getRotation(), up, down, left, right);
            const int dir = right ? 1 : left ? -1 : up ? PlatformManager::TIERS_PER_LOOP
                          : down ? -PlatformManager::TIERS_PER_LOOP : 0;
            bool stepped = false;
            const unsigned long now = millis();
            if (dir != _pickDir) {
                _pickDir = dir;
                _pickRepeatAt = now + PICK_REPEAT_DELAY_MS;
                stepped = dir != 0;
            } else if (dir != 0 && (long)(now - _pickRepeatAt) >= 0) {
                _pickRepeatAt = now + PICK_REPEAT_MS;
                stepped = true;
            }
            if (stepped) {
                _pick = (_pick - 1 + dir + PICK_STAGES) % PICK_STAGES + 1;
                _pickAt = now;
                previewStage();
                audio.playTone(1200, 15);
            }
            renderPicker(canvas);
            flushLandscape(canvas);
            return true;
        }

        // ---- PHASE: PLAYING ----
        if (_phase == PHASE_PLAYING) {
            bool uiNeedsUpdate = false;
            bool playerHit     = false;

            _particles.update();
            _platforms.update();
            const int nightEvents = _platforms.updateNight(_player.getX());
            if (nightEvents & 1) sfx(SFX_DART);
            if (nightEvents & 2) sfx(SFX_JET);
            _backdrop.update(_platforms.getScrollSpeed());
            updatePopups(_platforms.getScrollSpeed());
            _platforms.advanceDifficulty();
            checkStageProgress();

            // Terrain/hazard progression (see PlatformManager class comment
            // and ArcadeConfig's RUNNER_*_TIER constants for the full map):
            // tier 0/1 ground+fire pits -> tier 2 platform gaps -> tier 3
            // + moving platforms -> tier 4 + flying enemy (early preview)
            // -> tier 5 ground again with stairs+spikes, no ships -> tier 6
            // + rolling boulders, still no ships -> tier 7 ships return.
            // The enemy is capped at 1 and toggles on/off with tier rather
            // than unlocking once, so it can step aside for tiers 5-6.
            int tier = _platforms.getTier();
            const bool bossTier = tier == PlatformManager::BOSS_TIER;   // the boss has the sky to itself
            bool shipsActive = (tier == ArcadeConfig::RUNNER_EARLY_SHIP_TIER) ||
                               (tier >= ArcadeConfig::RUNNER_ENEMY_TIER && !bossTier);
            _enemies.setActive(shipsActive);

            // Jump buffer: a press just before landing still jumps on landing.
            if (in.btnAPressed) _jumpPressedAt = millis();
            if (_jumpPressedAt != 0) {
                if (millis() - _jumpPressedAt > ArcadeConfig::RUNNER_JUMP_BUFFER_MS) {
                    _jumpPressedAt = 0;
                } else if (_player.jump()) {
                    _jumpPressedAt = 0;
                    sfx(SFX_JUMP);
                }
            }
            // B held ducks (on the ground; a jump this frame stands it up).
            if (_player.setDucking(in.btnB)) sfx(SFX_DUCK);

            // Joystick nudges the runner forward/back within a bounded range —
            // rotation-1 games read joyY for on-screen horizontal, same swap
            // AsteroidFlux uses for its physical orientation.
            const float prevOffset = _playerXOffset;
            _playerXOffset += in.joyY * ArcadeConfig::RUNNER_X_MOVE_SPEED;
            _playerXOffset  = constrain(_playerXOffset, (float)ArcadeConfig::RUNNER_X_MIN_OFFSET,
                                        offsetCeiling(prevOffset));
            _player.setX((float)ArcadeConfig::RUNNER_BASE_X + _playerXOffset);

            // While levitating, joyX (otherwise unused in this game) drives
            // free vertical movement instead of gravity/ground collision.
            if (_player.isLevitating()) {
                _player.moveVertical(in.joyX * ArcadeConfig::RUNNER_LEVITATE_SPEED);
                _player.keepAbove((float)_platforms.surfaceYNear(_player.getX(), _player.getX() + RUNNER_WIDTH));
                if (millis() % 120 < 20) {
                    _particles.spawnFire(_player.getX() + RUNNER_WIDTH / 2.0f,
                                         _player.getY() + RUNNER_HEIGHT,
                                         0.0f, 0.3f, ArcadeConfig::COLOR_ION_BLUE, 2);   // a short trail
                }
            }
            // If levitation just ended, physics below resumes falling
            // naturally from wherever the player currently is.
            _player.updateLevitation();

            float px = _player.getX(), pRight = px + RUNNER_WIDTH;
            float py = _player.getY(), pBottom = py + RUNNER_HEIGHT;
            const float pTop = _player.hitTop();   // lower while ducked
            int ground = _platforms.groundYAt(px, pRight, py, pBottom);
            float groundTarget = (ground == -1) ? (float)(ArcadeConfig::LANDSCAPE_HEIGHT + 40) : (float)ground;

            _player.updatePhysics(groundTarget);
            _player.updateAnimation();
            _player.updateInvincibility();

            _powerUp.maybeSpawn(tier, ArcadeConfig::LANDSCAPE_WIDTH, _platforms, _player.isInvincible() || bossTier);
            if (_powerUp.update(_platforms.getScrollSpeed(), _player, _particles, uiNeedsUpdate)) sfx(SFX_STAR);

            float firePitX;
            bool hasFirePitAhead = _platforms.upcomingFirePitX(firePitX);
            _levitationPowerUp.maybeSpawn(tier, _platforms.getLoop(), ArcadeConfig::LANDSCAPE_WIDTH, _platforms,
                                          hasFirePitAhead, firePitX);
            if (_levitationPowerUp.update(_platforms.getScrollSpeed(), _player, _particles, uiNeedsUpdate, _platforms)) sfx(SFX_FLY);

            if (_enemies.update(_platforms.getScrollSpeed(), _player, _particles, playerHit)) sfx(SFX_ROCK);
            if (_boulders.update(tier, _platforms.getScrollSpeed(), _platforms, _player, _particles, playerHit,
                                 _platforms.nightAhead(_player.getX()) || bossTier)) sfx(SFX_BOULDER);

            // The boss stretch: its ship and its rocks.
            if (_boss.update(_platforms.getScrollSpeed(), _platforms.framesLeftInStage(), _player, _platforms,
                             _particles, playerHit)) sfx(SFX_DROP);

            // Night's beams, jets and darts: duck under them.
            if (_platforms.nightHits(px, pRight, pTop, pBottom) && !_player.isInvincible()) {
                _particles.spawnExplosion(px + RUNNER_WIDTH / 2.0f, pTop, ArcadeConfig::COLOR_ORANGE, 6);
                playerHit = true;
            }

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

            // Score ticks with distance travelled: PlatformManager's
            // distance counter, the one stages are measured in.
            if (_platforms.getDistance() % ArcadeConfig::RUNNER_SCORE_FRAMES == 0) {
                _score++;
                uiNeedsUpdate = true;
            }

            if (fellOffScreen || (playerHit && !_player.isInvincible())) {
                triggerPlayerDeath();
                _bannerUntil = 0;
                if (!_demo) {                // a demo death just ends the demo
                    _lives--;
                    _diedThisStage = true;
                }
                _phase = PHASE_DEATH;
                _phaseTimer = millis();

                renderWorld(canvas);
                drawUI(canvas);
                flushLandscape(canvas);
                return true;
            }

            scoreClearedHazards();
            renderWorld(canvas);
            _player.render(canvas);
            drawPopups(canvas);
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
            renderWorld(canvas);   // stopped where it was, under the burst
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
                // Out of lives: a name for the table if the score made it.
                _particles.clearAll();
                _scores.forget();
                _phase             = !_test && _scores.offer(_score) ? PHASE_NAME : PHASE_GAMEOVER;
                _gameOverEnteredMs = millis();
                audio.stopLoop();
                sfx(SFX_OVER);
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
            canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH / 4, 55);
            canvas.print("STAGE: "); canvas.print(_stage);

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
            // A test run plays again from its stage.
            if (inputOk && input.btnAPressed) { startNewGame(audio, _test ? _testFrom : 1, _test); return true; }

            if (elapsed > GAMEOVER_TIMEOUT_MS) {
                // Back to the title, from the start of the attract cycle
                // (whichever screen the game was started from).
                _phase             = PHASE_ATTRACT;
                _attractSlide      = SLIDE_SPLASH;
                _attractSlideTimer = millis();
                _test              = false;
            }
            return true;
        }

        return true;
    }

    // Quitting (the Back button): a game in progress still goes on the
    // table, under the last name entered; a name being entered is kept.
    void onQuit(AudioEngine &audio) override {
        if (_phase == PHASE_NAME) _scores.finishNow();
        else if (!_demo && !_test && (_phase == PHASE_PLAYING || _phase == PHASE_DEATH)) _scores.record(_score);
        audio.mute();
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
