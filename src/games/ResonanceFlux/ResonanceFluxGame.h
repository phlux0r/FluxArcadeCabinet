#ifndef RESONANCE_FLUX_GAME_H
#define RESONANCE_FLUX_GAME_H

#include <stdlib.h>
#include <string.h>
#include <esp_heap_caps.h>
#include <Preferences.h>
#include "../../games/IGame.h"
#include "../../cabinet/ArcadeConfig.h"
#include "../../cabinet/AudioEngine.h"
#include "../../cabinet/HighScores.h"
#include "ResonanceConfig.h"
#include "ResonanceFigure.h"

// =============================================================================
// RESONANCE FLUX: a green phosphor oscilloscope, landscape.
// The stick tunes your Lissajous figure: left/right steps the dial round
// its ring of ratios, up/down turns the phase. Signals weave in towards
// it, each a figure of its own. On a signal's ratio with its phase
// matched, both glow: A shatters it. Signals that reach you add static
// (noise on the scope, hiss, a wander in your phase), and at 100 static
// the game's over. B dampens: everything slows for a while. You hear your
// ratio's note and a signal's, the same note when you're on its ratio
// (the mixer's hum, audio/AudioMixer.h).
//
// Every fifth wave is a Chord (the boss, ResonanceChord.h).
// docs/design/ResonanceFlux.md has the design and how it changed in play
// (cascades, chains and the other signal types were left out). Left alone, the title cycles through three how-to slides,
// the high scores and a silent demo; B held with A on them opens the wave
// select, for test runs that put nothing on the table, and B on its own
// the options (the ratio hint, the notes, the pace, bosses, the debug
// line), which set a score multiplier. No music: the two
// notes are the soundtrack, and music would hide them.
//
// Files: ResonanceFluxGame.h (phases and play), ResonanceScope.h (the
// afterglow and drawing), ResonanceAutopilot.h (a player for the harness),
// ResonanceFigure.h (the figure maths), ResonanceConfig.h.
// =============================================================================

namespace resonance {

class ResonanceFluxGame : public IGame {
public:
    ~ResonanceFluxGame() override { freeGlow(); }
    void init(AudioEngine &audio) override;
    bool update(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) override;
    void onQuit(AudioEngine &audio) override;
    void onExit() override { freeGlow(); }
    uint8_t getRotation() const override { return 1; }
    const char* getName() const override { return "Resonance Flux"; }

private:
    // PICK: the wave select, choosing a wave to start a test run on.
    // OPTIONS: the options screen.
    enum Phase : uint8_t { PHASE_ATTRACT, PHASE_PICK, PHASE_OPTIONS, PHASE_PLAYING, PHASE_NAME, PHASE_GAMEOVER };
    // The options, saved on the cabinet (namespace "res_opts").
    enum Pace : uint8_t { PACE_CALM, PACE_NORMAL, PACE_FAST };
    enum OptRow : uint8_t { OPT_HINT, OPT_NOTES, OPT_PACE, OPT_BOSSES, OPT_DEBUG, OPT_COUNT };
    struct Options {
        bool hint = true, notes = true, bosses = true, debug = DEBUG_LINE;
        uint8_t pace = PACE_NORMAL;
    };
    enum Slide : uint8_t { SLIDE_TITLE, SLIDE_TUNE, SLIDE_MATCH, SLIDE_STATIC, SLIDE_SCORES, SLIDE_DEMO };
    // Within a game: the wave's number showing, play, and the wave's tally.
    enum Round : uint8_t { ROUND_INTRO, ROUND_PLAY, ROUND_CLEAR };
    // The optional sounds (RES_SFX below has their files and fallbacks).
    enum Sfx : uint8_t { SFX_TITLE, SFX_START, SFX_WAVE, SFX_LOCK, SFX_SHATTER, SFX_MISS, SFX_HIT,
                         SFX_DAMP, SFX_CLEAR, SFX_OVER, SFX_CHORD_WARN, SFX_CHORD_HIT, SFX_CHORD_DOWN,
                         SFX_COUNT };

    struct Signal {
        bool alive = false;
        uint8_t ratio = 0;            // into RATIOS
        float phase = 0;
        float x = 0, y = 0;
        float speed = 0;              // px/s along its weaving course
        float sway = 0, swayRate = 0, swayAt = 0;   // radians, radians/s, where in the sway
    };
    // The Chord: its layers' ratios and phases (0 the brightest), how many
    // are stripped (the brightest left is ratio[stripped]), its phase
    // drift and the middle layer's morph, which side it's on.
    struct Chord {
        bool active = false;
        uint8_t ratio[CHORD_LAYERS] = {};
        float phase[CHORD_LAYERS] = {}, drift[CHORD_LAYERS] = {};
        int stripped = 0;
        bool morph = false, left = false;
        uint8_t morphA = 0, morphB = 0;
        unsigned long morphAt = 0, startedAt = 0, sendAt = 0;
    };
    struct Shard { float x = 0, y = 0, vx = 0, vy = 0; unsigned long until = 0; bool white = false; };

    // ---- Phases and play (this file) ----
    void enterAttract();
    bool updateAttract(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    void startDemo();
    void endDemo();
    void enterOptions();
    bool updateOptions(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    void toggleOption(int row, int dir);
    void loadOptions();
    void saveOptions() const;
    float scoreMult() const;
    float paceSpeed() const { return _opt.pace == PACE_CALM ? PACE_CALM_SPEED : _opt.pace == PACE_FAST ? PACE_FAST_SPEED : 1.0f; }
    void enterPicker();
    bool updatePicker(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    void resetRun(int wave);
    void startGame(AudioEngine &audio, int first = -1);
    void startWave(int wave);
    void enterGameOver(AudioEngine &audio);
    void updateFrameTime();
    void stepPlay(const InputState &in);
    void tune(const InputState &in);
    void spawnSignal();
    void moveSignals();
    void findMatch();
    void fire();
    void shatter(int i);
    void burst(int i);
    void updateShards();
    void addStatic(float s);
    void updateHum(AudioEngine &audio);
    void buildStops(int wave);
    void step(int dir);
    const Ratio &yourRatio() const { return RATIOS[_stops[_stop]]; }
    static float pitchOf(int ratio) { return HUM_F_LO + HUM_F_STEP * ratio; }
    float phaseEff() const { return _phase + _jitP; }
    bool inPlay() const { return _phaseState == PHASE_PLAYING || _demo; }   // a game or the demo
    int  aliveCount() const;
    int  maxOnScope() const;
    float driftSpeed() const;
    static float curve(float v);
    static float frand(float lo, float hi);
    void findSounds(AudioEngine &audio);
    void sfx(Sfx s);

    // ---- The Chord (ResonanceChord.h) ----
    bool isChordWave(int wave) const;
    void setupChord();
    float chordX() const;
    void updateChord();
    void findChordMatch();
    void hitChord(int layer);
    void chordShards(int layer, bool white);

    // ---- The scope and drawing (ResonanceScope.h) ----
    bool allocGlow();
    void freeGlow();
    void buildLut();
    void fadeGlow();
    void plot(uint8_t *plane, int x, int y, uint8_t v);
    void line(uint8_t *plane, int x0, int y0, int x1, int y1, uint8_t v);
    void drawFigure(uint8_t *a, uint8_t *b, const Ratio &r, float phase, float cx, float cy, float rad, bool big,
                    uint8_t v = 255);
    void drawChord(uint8_t *green, uint8_t *amber);
    void pushGlow(GFXcanvas16 &cv);
    void renderPlay(GFXcanvas16 &cv);
    void renderTitle(GFXcanvas16 &cv);
    void renderInfo(GFXcanvas16 &cv, int page);
    void renderScores(GFXcanvas16 &cv);
    void renderPicker(GFXcanvas16 &cv);
    void renderOptions(GFXcanvas16 &cv);
    void drawDemoOverlay(GFXcanvas16 &cv);
    void figureFor(const Ratio &r, float phase, float cx, float cy, float rad, bool amber, bool white);
    void renderGameOver(GFXcanvas16 &cv);
    void drawHud(GFXcanvas16 &cv);
    void drawStrip(GFXcanvas16 &cv);
    int  dialX(int stop) const;
    void drawOverlays(GFXcanvas16 &cv);

    // ---- The harness's player (ResonanceAutopilot.h) ----
    InputState autopilot();

    // ---- State ----
    hiscore::ScoreBoard _scores;
    Signal _signals[MAX_SIGNALS];
    Chord  _chord;
    int    _chordFocus = -1, _chordMatched = -1;   // a layer on your stop nearest your phase; in resonance
    float  _chordGap = 1;
    Shard  _shards[MAX_SHARDS];
    uint8_t *_glow = nullptr;            // two planes, green then amber, W x SCOPE_H each
    uint16_t _lut[32 * 32];              // (green, amber) -> colour

    Phase _phaseState = PHASE_ATTRACT;
    Slide _slide = SLIDE_TITLE;
    unsigned long _slideAt = 0;
    Round _round = ROUND_INTRO;
    unsigned long _phaseAt = 0, _roundAt = 0;
    unsigned long _now = 0, _lastFrameMs = 0;
    float _dt = 1.0f / 30.0f;

    // The dial's stops: ratios (into RATIOS) in order, and each ratio's
    // stop (-1 if it hasn't got one yet).
    uint8_t _stops[RATIO_COUNT] = {};
    int8_t  _stopOf[RATIO_COUNT] = {};
    int     _stopCount = 0;

    // You: the stop you're on and the phase as tuned, and static's wander
    // on the phase. The stick's last step and when a held one repeats.
    int   _stop = 0;
    float _phase = 0;
    float _jitP = 0, _jitTP = 0;
    int   _stepDir = 0;
    unsigned long _stepRepeatAt = 0;
    unsigned long _jitAt = 0;

    long  _score = 0, _clearBonus = 0;
    int   _wave = 1, _toSpawn = 0, _dampens = DAMPEN_PER_WAVE;
    float _static = 0;
    unsigned long _spawnAt = 0, _dampUntil = 0, _fireReadyAt = 0;
    // On your stop, the signal nearest your phase (-1 if none is), and it
    // again if it's in resonance; the signal nearest the core.
    int   _focus = -1, _matched = -1, _threat = -1;
    float _focusPhaseGap = 1;
    float _beamX = 0, _beamY = 0;
    unsigned long _beamUntil = 0, _hitFlashUntil = 0;
    bool  _sfxOnCard[SFX_COUNT] = {}, _fallbackOnCard[SFX_COUNT] = {};
    const char *_lastSfx = "";           // what the last sfx() played, for the harness
    bool  _wasMatched = false;           // for the lock sound, as resonance begins
    float _targetHz = 400;               // the focus signal's tone

    // Title: the figure it morphs through.
    float _titleAt = 0, _titlePhase = 0;    // the title's ratio (in RATIOS, as it climbs) and phase

    // The wave select: a test run puts nothing on the table.
    bool _test = false;
    int  _testFrom = 1;                  // its wave (A at its game over starts it again)
    int  _pick = 1, _pickDir = 0;        // the picker's wave; the stick's last step
    unsigned long _pickRepeatAt = 0, _pickAt = 0;

    // The options, and the screen's row, the stick's last move and when
    // it was last touched. B on the attract screens opens it on the
    // release, so B held with A (the wave select) doesn't.
    Options _opt;
    int  _optRow = 0, _optDir = 0;
    unsigned long _optAt = 0;
    bool _bAlone = false;                // B down, and A not pressed with it yet
    float _youLevel = 0, _targetLevel = 0;   // the hum's levels last set, for the harness

    // The demo.
    bool _demo = false;
    unsigned long _demoUntil = 0;

    // Autopilot: A, B and a dial step held last frame; the signal it's
    // after, and when it reacts to a new one.
    bool _apPrevA = false, _apPrevB = false, _apPrevStep = false;
    int  _apTarget = -1;
    unsigned long _apReadyAt = 0;

    // Running totals, for the host harness (test/resonanceflux_harness.cpp).
    long _statShatters = 0, _statMisfires = 0, _statHits = 0, _statWaves = 0, _statDampens = 0;
    long _statLayers = 0, _statChords = 0;

    AudioEngine *_audio = nullptr;
};

// =============================================================================

inline float ResonanceFluxGame::curve(float v) {
    const float m = powf(fabsf(v), STICK_CURVE);
    return v < 0 ? -m : m;
}

inline float ResonanceFluxGame::frand(float lo, float hi) {
    return lo + (hi - lo) * (float)random(0, 10001) / 10000.0f;
}

namespace {
// The optional sounds, by Sfx: the file, a shared file to use instead if
// it isn't on the card, and the tone or melody that plays if neither is
// (or nothing, for the two that only play from the card: the wave's start
// and the moment of resonance, which had no sound before the WAVs).
struct SfxDef { const char *path, *fallback; int hz, ms; const int *notes, *durs; int len; };
const int RES_TITLE_N[] = { 330, 495, 660, 990 },  RES_TITLE_D[] = {  90,  90,  90, 220 };
const int RES_CLEAR_N[] = { 660, 880, 1320 },      RES_CLEAR_D[] = {  80,  80,  200 };
const int RES_OVER_N[]  = { 440, 415, 392, 220 },  RES_OVER_D[]  = { 140, 140, 140, 400 };
const int RES_WARN_N[]  = { 220, 0, 220, 0, 220 }, RES_WARN_D[]  = { 150, 80, 150, 80, 300 };   // 0: a rest
const int RES_DOWN_N[]  = { 1320, 990, 660, 440, 880 }, RES_DOWN_D[] = { 90, 90, 90, 90, 400 };
const SfxDef RES_SFX[] = {
    { "/audio/res_title.wav",   nullptr, 0, 0, RES_TITLE_N, RES_TITLE_D, 4 },   // SFX_TITLE
    { "/audio/res_start.wav",   nullptr, 900, 80, nullptr, nullptr, 0 },        // SFX_START
    { "/audio/res_wave.wav",    nullptr, 0, 0, nullptr, nullptr, 0 },           // SFX_WAVE (card only)
    { "/audio/res_lock.wav",    nullptr, 0, 0, nullptr, nullptr, 0 },           // SFX_LOCK (card only)
    { "/audio/res_shatter.wav", "/audio/explosion.wav", 1400, 60, nullptr, nullptr, 0 },   // SFX_SHATTER
    { "/audio/res_miss.wav",    nullptr, 140, 90, nullptr, nullptr, 0 },        // SFX_MISS
    { "/audio/res_hit.wav",     nullptr, 90, 160, nullptr, nullptr, 0 },        // SFX_HIT
    { "/audio/res_damp.wav",    nullptr, 220, 200, nullptr, nullptr, 0 },       // SFX_DAMP
    { "/audio/res_clear.wav",   nullptr, 0, 0, RES_CLEAR_N, RES_CLEAR_D, 3 },   // SFX_CLEAR
    { "/audio/res_over.wav",    nullptr, 0, 0, RES_OVER_N, RES_OVER_D, 4 },     // SFX_OVER
    { "/audio/res_chord_warn.wav", nullptr, 0, 0, RES_WARN_N, RES_WARN_D, 5 },  // SFX_CHORD_WARN
    { "/audio/res_chord_hit.wav", nullptr, 600, 80, nullptr, nullptr, 0 },      // SFX_CHORD_HIT
    { "/audio/res_chord_down.wav", "/audio/star_boss_die.wav", 0, 0, RES_DOWN_N, RES_DOWN_D, 5 },   // SFX_CHORD_DOWN
};
static_assert(sizeof(RES_SFX) / sizeof(RES_SFX[0]) == 13, "one SfxDef per Sfx");
}  // namespace

// Which optional sounds are on the card, checked once: a missing file
// would otherwise cost an SD open every time it's asked for. What will
// play is decoded into the mixer's cache now, so the first play isn't late.
inline void ResonanceFluxGame::findSounds(AudioEngine &audio) {
    for (int i = 0; i < SFX_COUNT; ++i) {
        _sfxOnCard[i] = audio.exists(RES_SFX[i].path);
        _fallbackOnCard[i] = !_sfxOnCard[i] && RES_SFX[i].fallback && audio.exists(RES_SFX[i].fallback);
        if (_sfxOnCard[i]) audio.preload(RES_SFX[i].path);
        else if (_fallbackOnCard[i]) audio.preload(RES_SFX[i].fallback);
    }
}

// The file if it's on the card, else its fallback file, else the tone or
// melody. In the demo the engine is silenced, so nothing plays.
inline void ResonanceFluxGame::sfx(Sfx s) {
    if (!_audio) return;
    const SfxDef &d = RES_SFX[s];
    if (_sfxOnCard[s])           { _audio->playWAV(d.path); _lastSfx = d.path; }
    else if (_fallbackOnCard[s]) { _audio->playWAV(d.fallback); _lastSfx = d.fallback; }
    else if (d.notes)            { _audio->playMelody(d.notes, d.durs, d.len); _lastSfx = "melody"; }
    else if (d.hz)               { _audio->playTone(d.hz, d.ms); _lastSfx = "tone"; }
    else                         _lastSfx = "none";
}

inline void ResonanceFluxGame::init(AudioEngine &audio) {
    _audio = &audio;
    _scores.begin("resonance");
    allocGlow();
    buildLut();
    findSounds(audio);
    loadOptions();
    _lastFrameMs = millis();
    _demo = _test = false;
    resetRun(1);
    enterAttract();
    sfx(SFX_TITLE);
}

inline void ResonanceFluxGame::updateFrameTime() {
    _now = millis();
    unsigned long dt = _now - _lastFrameMs;
    _lastFrameMs = _now;
    if (dt < 10) dt = 10;
    if (dt > 50) dt = 50;
    _dt = dt / 1000.0f;
}

inline bool ResonanceFluxGame::update(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    _audio = &audio;
    updateFrameTime();
    if (!_glow && !allocGlow()) {        // no RAM for the scope: nothing to play
        canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
        hiscore::printCentred(canvas, "OUT OF MEMORY", 60, ArcadeConfig::COLOR_RED);
        return true;
    }

    switch (_phaseState) {
        case PHASE_ATTRACT: return updateAttract(canvas, input, audio);
        case PHASE_PICK:    return updatePicker(canvas, input, audio);
        case PHASE_OPTIONS: return updateOptions(canvas, input, audio);
        case PHASE_NAME:
            updateShards();
            renderPlay(canvas);
            _scores.draw(canvas);
            if (_scores.update(input, getRotation())) {
                _phaseState = PHASE_GAMEOVER;
                _phaseAt = _now;
                audio.playTone(1047, 80);
            }
            return true;
        case PHASE_GAMEOVER: {
            updateShards();
            renderGameOver(canvas);
            const unsigned long t = _now - _phaseAt;
            if (t > GAMEOVER_MIN_MS && input.btnAPressed) startGame(audio, _test ? _testFrom : -1);
            else if (t > GAMEOVER_TIMEOUT_MS) { resetRun(1); enterAttract(); }
            return true;
        }
        default: break;
    }
    stepPlay(input);
    updateHum(audio);
    renderPlay(canvas);
    if (_static >= 100.0f) enterGameOver(audio);
    return true;
}

// Quitting (the Back button): a game in progress still goes on the table
// (not a test run's); a name being entered is kept.
inline void ResonanceFluxGame::onQuit(AudioEngine &audio) {
    if (_phaseState == PHASE_NAME) _scores.finishNow();
    else if (_phaseState == PHASE_PLAYING && !_test) _scores.record(_score);
    audio.mute();
}

inline void ResonanceFluxGame::enterAttract() {
    _phaseState = PHASE_ATTRACT;
    _test = false;
    _slide = SLIDE_TITLE;
    _phaseAt = _slideAt = _now = millis();
}

// Title, three how-to slides and the high scores (ATTRACT_SLIDE_MS each),
// then the demo, round and round. A starts a game from any of them; B held
// with A (not in the demo) opens the wave select, and B pressed and let go
// on its own the options.
inline bool ResonanceFluxGame::updateAttract(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    if (input.btnBPressed) _bAlone = true;
    if (input.btnAPressed && input.btnB && _slide != SLIDE_DEMO) {
        _bAlone = false;
        enterPicker();
        renderPicker(canvas);
        return true;
    }
    if (input.btnBReleased && _bAlone && _slide != SLIDE_DEMO) {
        _bAlone = false;
        enterOptions();
        renderOptions(canvas);
        return true;
    }
    if (!input.btnB) _bAlone = false;
    if (input.btnAPressed) {
        if (_demo) endDemo();
        startGame(audio);
        renderPlay(canvas);
        return true;
    }
    if (_slide == SLIDE_DEMO) {
        if (_now >= _demoUntil || _static >= 100.0f) {
            endDemo();
            renderTitle(canvas);
            return true;
        }
        // Silent: new sounds are dropped and the hum held at nothing,
        // lifted again whichever way this returns.
        struct Quiet { AudioEngine &a; Quiet(AudioEngine &a_) : a(a_) { a.setSilenced(true); }
                       ~Quiet() { a.setSilenced(false); } } quiet(audio);
        stepPlay(autopilot());
        updateHum(audio);
        renderPlay(canvas);
        drawDemoOverlay(canvas);
        return true;
    }
    audio.stopHum();
    if (_now - _slideAt > ATTRACT_SLIDE_MS) {
        _slideAt = _now;
        if (_slide == SLIDE_SCORES) { startDemo(); renderPlay(canvas); return true; }
        _slide = (Slide)(_slide + 1);
    }
    switch (_slide) {
        case SLIDE_TITLE:  renderTitle(canvas); break;
        case SLIDE_TUNE:   renderInfo(canvas, 0); break;
        case SLIDE_MATCH:  renderInfo(canvas, 1); break;
        case SLIDE_STATIC: renderInfo(canvas, 2); break;
        default:           renderScores(canvas); break;
    }
    return true;
}

// The demo: a random early wave, the autopilot playing it silently.
inline void ResonanceFluxGame::startDemo() {
    _demo = true;
    _slide = SLIDE_DEMO;
    resetRun((int)random(1, DEMO_MAX_WAVE + 1));
    _round = ROUND_PLAY;                     // straight into it, no banner
    _spawnAt = _now;
    _demoUntil = _now + (unsigned long)random((long)DEMO_MIN_MS, (long)DEMO_MAX_MS + 1);
}

// Back to the title, leaving nothing of the demo behind.
inline void ResonanceFluxGame::endDemo() {
    _demo = false;
    resetRun(1);
    enterAttract();
}

// The wave select, for trying any wave without playing up to it: left and
// right step one wave, up and down five, each shown with its dial; A
// starts a test run there, B goes back to the title, as does leaving it.
// The options screen: up/down picks a line, left/right or A changes it, B
// goes back, as does leaving it alone. Each change is saved at once.
inline void ResonanceFluxGame::enterOptions() {
    _phaseState = PHASE_OPTIONS;
    if (_glow) memset(_glow, 0, 2 * W * SCOPE_H);
    _optRow = 0;
    _optDir = 0;
    _optAt = _now;
}

inline bool ResonanceFluxGame::updateOptions(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    audio.stopHum();
    if (input.btnBPressed || _now - _optAt > PICK_TIMEOUT_MS) {
        enterAttract();
        renderTitle(canvas);
        return true;
    }
    bool up, down, left, right;
    hiscore::screenDirs(input, getRotation(), up, down, left, right);
    const int dir = up ? -1 : down ? 1 : left ? -2 : right ? 2 : 0;   // +-1 rows, +-2 change
    if (dir != _optDir) {
        _optDir = dir;
        if (dir == 1 || dir == -1) _optRow = (_optRow + dir + OPT_COUNT) % OPT_COUNT;
        else if (dir) toggleOption(_optRow, dir / 2);
        if (dir) { _optAt = _now; audio.playTone(1200, 15); }
    }
    if (input.btnAPressed) { toggleOption(_optRow, 1); _optAt = _now; audio.playTone(1200, 15); }
    renderOptions(canvas);
    return true;
}

inline void ResonanceFluxGame::toggleOption(int row, int dir) {
    switch (row) {
        case OPT_HINT:  _opt.hint = !_opt.hint; break;
        case OPT_NOTES: _opt.notes = !_opt.notes; break;
        case OPT_PACE:  _opt.pace = (uint8_t)((_opt.pace + (dir < 0 ? 2 : 1)) % 3); break;
        case OPT_BOSSES: _opt.bosses = !_opt.bosses; break;
        case OPT_DEBUG: _opt.debug = !_opt.debug; break;
        default: return;
    }
    saveOptions();
}

inline void ResonanceFluxGame::loadOptions() {
    Preferences p;
    p.begin("res_opts", true);
    _opt.hint = p.getBool("hint", true);
    _opt.notes = p.getBool("notes", true);
    _opt.bosses = p.getBool("bosses", true);
    _opt.debug = p.getBool("debug", DEBUG_LINE);
    const int pace = p.getInt("pace", PACE_NORMAL);
    _opt.pace = (uint8_t)(pace >= PACE_CALM && pace <= PACE_FAST ? pace : PACE_NORMAL);
    p.end();
}

inline void ResonanceFluxGame::saveOptions() const {
    Preferences p;
    p.begin("res_opts", false);
    p.putBool("hint", _opt.hint);
    p.putBool("notes", _opt.notes);
    p.putBool("bosses", _opt.bosses);
    p.putBool("debug", _opt.debug);
    p.putInt("pace", _opt.pace);
    p.end();
}

// Points are worth more played with less help: x1.25 for each help off
// (the hint, the notes), x1.5 at the fast pace, x0.75 at the calm one.
inline float ResonanceFluxGame::scoreMult() const {
    float m = 1.0f;
    if (!_opt.hint) m *= MULT_NO_HINT;
    if (!_opt.notes) m *= MULT_NO_NOTES;
    if (_opt.pace == PACE_FAST) m *= MULT_FAST;
    if (_opt.pace == PACE_CALM) m *= MULT_CALM;
    return m;
}

inline void ResonanceFluxGame::enterPicker() {
    _phaseState = PHASE_PICK;
    if (_glow) memset(_glow, 0, 2 * W * SCOPE_H);   // no title left glowing behind it
    _pick = 1;
    _pickDir = 0;
    _pickAt = _now;
}

inline bool ResonanceFluxGame::updatePicker(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    audio.stopHum();
    if (input.btnAPressed) {
        startGame(audio, _pick);
        renderPlay(canvas);
        return true;
    }
    if (input.btnBPressed || _now - _pickAt > PICK_TIMEOUT_MS) {
        enterAttract();
        renderTitle(canvas);
        return true;
    }
    bool up, down, left, right;
    hiscore::screenDirs(input, getRotation(), up, down, left, right);
    const int dir = right ? 1 : left ? -1 : up ? 5 : down ? -5 : 0;
    bool stepNow = false;
    if (dir != _pickDir) {
        _pickDir = dir;
        _pickRepeatAt = _now + STEP_REPEAT_DELAY_MS;
        stepNow = dir != 0;
    } else if (dir != 0 && (long)(_now - _pickRepeatAt) >= 0) {
        _pickRepeatAt = _now + STEP_REPEAT_MS;
        stepNow = true;
    }
    if (stepNow) {
        _pick = (_pick - 1 + dir + PICK_MAX_WAVE) % PICK_MAX_WAVE + 1;
        _pickAt = _now;
        audio.playTone(1200, 15);
    }
    renderPicker(canvas);
    return true;
}

// Everything a run starts from, shared by a real game, a test run and the
// demo.
inline void ResonanceFluxGame::resetRun(int wave) {
    _score = 0;
    _static = 0;
    _stopCount = 0;
    _stop = 0;
    _phase = 0;
    _jitP = _jitTP = 0;
    _stepDir = 0;
    _apTarget = -1;
    _apPrevA = _apPrevB = _apPrevStep = false;
    _scores.forget();
    for (auto &s : _shards) s.until = 0;
    startWave(wave);
}

// `first`, if given, is the wave select's: a test run from there that puts
// nothing on the table.
inline void ResonanceFluxGame::startGame(AudioEngine &audio, int first) {
    _test = first > 0;
    _testFrom = _test ? first : 1;
    resetRun(_testFrom);
    _phaseState = PHASE_PLAYING;
    _phaseAt = _now;
    sfx(SFX_START);
}

// The stops for the ratios a wave has reached. You stay on the ratio you
// were on, wherever its stop now sits.
inline void ResonanceFluxGame::buildStops(int wave) {
    const int keep = _stopCount > 0 ? _stops[_stop] : -1;
    _stopCount = 0;
    for (int i = 0; i < RATIO_COUNT; ++i) {
        _stopOf[i] = -1;
        if (RATIOS[i].firstWave <= wave) {
            _stopOf[i] = (int8_t)_stopCount;
            _stops[_stopCount++] = (uint8_t)i;
        }
    }
    _stop = keep >= 0 ? _stopOf[keep] : 0;
}

// One stop along, round from the last to the first and back.
inline void ResonanceFluxGame::step(int dir) {
    _stop = (_stop + dir + _stopCount) % _stopCount;
}

inline void ResonanceFluxGame::startWave(int wave) {
    _wave = wave;
    buildStops(wave);
    _toSpawn = 6 + 2 * wave;
    if (_toSpawn > 30) _toSpawn = 30;
    _chord.active = false;
    _chordFocus = _chordMatched = -1;
    if (isChordWave(wave)) { setupChord(); _toSpawn = 0; }   // its escorts come from it
    _dampens = DAMPEN_PER_WAVE;
    _dampUntil = 0;
    for (auto &s : _signals) s.alive = false;
    _focus = _matched = _threat = -1;
    _wasMatched = false;
    _round = ROUND_INTRO;
    _roundAt = _now;
}

// Static's reached 100: a name for the table first, if the score made it
// (never for a test run).
inline void ResonanceFluxGame::enterGameOver(AudioEngine &audio) {
    _phaseState = !_test && _scores.offer(_score) ? PHASE_NAME : PHASE_GAMEOVER;
    _phaseAt = _now;
    _static = 100;
    audio.stopHum();
    sfx(SFX_OVER);
}

inline int ResonanceFluxGame::aliveCount() const {
    int n = 0;
    for (auto &s : _signals) n += s.alive;
    return n;
}

inline int ResonanceFluxGame::maxOnScope() const {
    const int m = ON_SCOPE_FIRST + (_wave - 1) / ON_SCOPE_EVERY;
    return m < ON_SCOPE_MAX ? m : ON_SCOPE_MAX;
}

inline float ResonanceFluxGame::driftSpeed() const {
    const float s = DRIFT_START * powf(DRIFT_GROWTH, (float)(_wave - 1));
    return (s < DRIFT_MAX ? s : DRIFT_MAX) * paceSpeed();
}

inline void ResonanceFluxGame::addStatic(float s) {
    _static += s;
    if (_static < 0) _static = 0;
    if (_static > 100) _static = 100;
}

// One frame of play.
inline void ResonanceFluxGame::stepPlay(const InputState &in) {
    tune(in);
    updateShards();
    addStatic(-STATIC_DECAY * _dt);

    if (_round == ROUND_INTRO) {
        if (_now - _roundAt >= WAVE_INTRO_MS) {
            _round = ROUND_PLAY;
            _roundAt = _spawnAt = _now;
            if (_chord.active) { _chord.startedAt = _now; _chord.sendAt = _now + 1500; }
            sfx(_chord.active ? SFX_CHORD_WARN : SFX_WAVE);
        }
        return;
    }
    if (_round == ROUND_CLEAR) {
        if (_now - _roundAt >= WAVE_CLEAR_MS) startWave(_wave + 1);
        return;
    }

    if (_toSpawn > 0 && aliveCount() < maxOnScope() && (long)(_now - _spawnAt) >= 0) {
        spawnSignal();
        --_toSpawn;
        const long step = (long)SPAWN_FIRST_MS - (long)SPAWN_STEP_MS * (_wave - 1);
        const long gap = step > (long)SPAWN_MIN_MS ? step : (long)SPAWN_MIN_MS;
        _spawnAt = _now + (unsigned long)(gap / paceSpeed());   // arrivals closer together at a faster pace
    }
    updateChord();
    moveSignals();
    findMatch();
    findChordMatch();
    // Resonance worth firing on (not a Chord's dimmer layer) just begun.
    const bool matched = _matched >= 0 || (_chordMatched >= 0 && _chordMatched == _chord.stripped);
    if (matched && !_wasMatched) sfx(SFX_LOCK);
    _wasMatched = matched;
    if (in.btnAPressed) fire();
    if (in.btnBPressed && _dampens > 0 && _now >= _dampUntil) {
        --_dampens;
        _dampUntil = _now + DAMPEN_MS;
        ++_statDampens;
        sfx(SFX_DAMP);
    }
    if (_toSpawn == 0 && aliveCount() == 0 && !_chord.active) {
        _clearBonus = (long)((PTS_STATIC_LEFT * (long)(100.0f - _static) + PTS_DAMPEN_LEFT * _dampens) * scoreMult());
        _score += _clearBonus;
        addStatic(STATIC_CLEAR);
        ++_statWaves;
        _round = ROUND_CLEAR;
        _roundAt = _now;
        sfx(SFX_CLEAR);
    }
}

// Left/right steps the dial a stop at a time (a push past STEP_PUSH;
// held, it repeats after a pause, like a menu). Up/down turns the phase
// at a rate that follows the push, so letting go keeps it. Static makes
// the phase wander.
inline void ResonanceFluxGame::tune(const InputState &in) {
    const float turnDial = in.joyY;          // screen right: on round the dial
    const float turnPhase = -in.joyX;        // screen up
    const int dir = turnDial > STEP_PUSH ? 1 : turnDial < -STEP_PUSH ? -1 : 0;
    if (dir != _stepDir) {
        _stepDir = dir;
        if (dir) { step(dir); _stepRepeatAt = _now + STEP_REPEAT_DELAY_MS; }
    } else if (dir && (long)(_now - _stepRepeatAt) >= 0) {
        step(dir);
        _stepRepeatAt = _now + STEP_REPEAT_MS;
    }
    _phase += curve(turnPhase) * PHASE_SPEED * _dt;

    // Static's wander: towards a new random offset every 150ms.
    const float s = _static / 100.0f;
    if ((long)(_now - _jitAt) >= 0) {
        _jitAt = _now + 150;
        _jitTP = frand(-1, 1) * JITTER_PHASE * s;
    }
    float k = 6.0f * _dt;
    if (k > 1) k = 1;
    _jitP += (_jitTP - _jitP) * k;

    _phase = fmodf(_phase, TWO_PI_F);
    if (_phase < 0) _phase += TWO_PI_F;
}

// A signal at a random point on the scope's left or right edge, or the
// top or bottom within SPAWN_CORNER of a corner (the rest of those is too
// close to the core to give you time), heading for the core, its ratio
// from those this wave has reached.
inline void ResonanceFluxGame::spawnSignal() {
    Signal *slot = nullptr;
    for (auto &s : _signals) if (!s.alive) { slot = &s; break; }
    if (!slot) return;
    slot->ratio = _stops[random(0, _stopCount)];
    slot->phase = frand(0, TWO_PI_F);
    const float l = SIG_R + 1, r = W - SIG_R - 1, t = SCOPE_Y + SIG_R + 1, b = SCOPE_Y + SCOPE_H - SIG_R - 1;
    // Four corner stretches and two sides, chosen by length.
    const float corner = SPAWN_CORNER, vert = b - t;
    float p = frand(0, 4 * corner + 2 * vert);
    if (p < 4 * corner) {
        const int seg = (int)(p / corner) & 3;
        const float along = p - seg * corner;
        slot->x = (seg & 1) ? r - along : l + along;
        slot->y = (seg & 2) ? b : t;
    } else {
        p -= 4 * corner;
        slot->x = p < vert ? l : r;
        slot->y = t + (p < vert ? p : p - vert);
    }
    slot->speed = driftSpeed();
    slot->sway = frand(SWAY_MIN, SWAY_MAX);
    slot->swayRate = frand(SWAY_RATE_MIN, SWAY_RATE_MAX);
    slot->swayAt = frand(0, TWO_PI_F);
    slot->alive = true;
}

inline void ResonanceFluxGame::moveSignals() {
    const float slow = _now < _dampUntil ? DAMPEN_SLOW : 1.0f;
    for (int i = 0; i < MAX_SIGNALS; ++i) {
        Signal &s = _signals[i];
        if (!s.alive) continue;
        // Weave: the heading for the core, swung either side of it.
        s.swayAt += s.swayRate * slow * _dt;
        const float head = atan2f(CORE_Y - s.y, CORE_X - s.x) + s.sway * sinf(s.swayAt);
        s.x += cosf(head) * s.speed * slow * _dt;
        s.y += sinf(head) * s.speed * slow * _dt;
        if (s.x < SIG_R) s.x = SIG_R;
        if (s.x > W - SIG_R) s.x = W - SIG_R;
        if (s.y < SCOPE_Y + SIG_R) s.y = SCOPE_Y + SIG_R;
        if (s.y > SCOPE_Y + SCOPE_H - SIG_R) s.y = SCOPE_Y + SCOPE_H - SIG_R;
        const float dx = s.x - CORE_X, dy = s.y - CORE_Y;
        if (dx * dx + dy * dy <= (CORE_R + SIG_R * 0.5f) * (CORE_R + SIG_R * 0.5f)) burst(i);
    }
}

// The focus: of the signals on your stop, the one nearest your phase; it's
// matched inside the phase tolerance. The threat: the signal nearest the
// core, whose tone you hear when no signal shares your stop.
inline void ResonanceFluxGame::findMatch() {
    _focus = _matched = _threat = -1;
    _focusPhaseGap = 1e9f;
    float nearest = 1e9f;
    const float p = phaseEff();
    const int mine = _stops[_stop];
    for (int i = 0; i < MAX_SIGNALS; ++i) {
        const Signal &s = _signals[i];
        if (!s.alive) continue;
        const float dx = s.x - CORE_X, dy = s.y - CORE_Y, d2 = dx * dx + dy * dy;
        if (d2 < nearest) { nearest = d2; _threat = i; }
        if (s.ratio != mine) continue;
        const float pg = phaseGap(RATIOS[s.ratio], p, s.phase);
        if (pg < _focusPhaseGap) { _focus = i; _focusPhaseGap = pg; }
    }
    if (_focus >= 0 && _focusPhaseGap < PHASE_TOL) _matched = _focus;
}

inline void ResonanceFluxGame::fire() {
    if ((long)(_now - _fireReadyAt) < 0) return;
    // The Chord's brightest layer first, then a signal, then (wrongly) a
    // dimmer layer.
    if (_chordMatched >= 0 && _chordMatched == _chord.stripped) { hitChord(_chordMatched); return; }
    if (_matched >= 0) { shatter(_matched); return; }
    if (_chordMatched >= 0) { hitChord(_chordMatched); return; }
    ++_statMisfires;
    addStatic(STATIC_MISFIRE);
    _fireReadyAt = _now + FIRE_COOLDOWN_MS;
    sfx(SFX_MISS);
}

// A shot in resonance: the signal's figure flies apart, worth up to double
// the further out it was.
inline void ResonanceFluxGame::shatter(int i) {
    Signal &s = _signals[i];
    const Ratio &r = RATIOS[s.ratio];
    const float dx = s.x - CORE_X, dy = s.y - CORE_Y;
    float far = (sqrtf(dx * dx + dy * dy) - CORE_R) / (70.0f - CORE_R);
    far = far < 0 ? 0 : far > 1 ? 1 : far;
    _score += (long)(PTS_TONE * (1.0f + far) * scoreMult());
    addStatic(STATIC_SHATTER);
    ++_statShatters;
    _beamX = s.x;
    _beamY = s.y;
    _beamUntil = _now + 120;
    const int n = segmentsFor(r, false);
    int made = 0;
    for (auto &sh : _shards) {
        if (sh.until > _now) continue;
        float fx, fy;
        figurePoint(r, s.phase, TWO_PI_F * made / n, fx, fy);
        sh.x = s.x + fx * SIG_R;
        sh.y = s.y - fy * SIG_R;
        sh.vx = fx * frand(25, 60) + frand(-8, 8);
        sh.vy = -fy * frand(25, 60) + frand(-8, 8);
        sh.until = _now + SHARD_MS - (unsigned long)random(0, 200);
        sh.white = (made & 3) == 0;
        if (++made >= n) break;
    }
    s.alive = false;
    if (_focus == i) _focus = _matched = -1;
    if (_threat == i) _threat = -1;
    sfx(SFX_SHATTER);
}

// A signal reached you: static, and a crackle of white.
inline void ResonanceFluxGame::burst(int i) {
    Signal &s = _signals[i];
    addStatic(STATIC_HIT);
    ++_statHits;
    _hitFlashUntil = _now + 150;
    int made = 0;
    for (auto &sh : _shards) {
        if (sh.until > _now) continue;
        const float a = frand(0, TWO_PI_F), v = frand(10, 50);
        sh.x = s.x;
        sh.y = s.y;
        sh.vx = cosf(a) * v;
        sh.vy = sinf(a) * v;
        sh.until = _now + 300 + (unsigned long)random(0, 200);
        sh.white = true;
        if (++made >= 16) break;
    }
    s.alive = false;
    if (_focus == i) _focus = _matched = -1;
    if (_threat == i) _threat = -1;
    sfx(SFX_HIT);
}

inline void ResonanceFluxGame::updateShards() {
    for (auto &sh : _shards) {
        if (sh.until <= _now) continue;
        sh.x += sh.vx * _dt;
        sh.y += sh.vy * _dt;
    }
}

// Your stop's tone always, and a signal's: the one on your stop if there
// is one (the same note: unison), else the one nearest the core (a
// different note, so you can hear you're on the wrong ratio). Hiss with
// the static.
inline void ResonanceFluxGame::updateHum(AudioEngine &audio) {
    // With the notes off (an option), only the hiss.
    _youLevel = _opt.notes ? HUM_YOU_LEVEL : 0;
    audio.setHum(0, pitchOf(_stops[_stop]), _youLevel);
    // The note: a signal on your stop, else a Chord layer on it, else the
    // signal nearest the core, else the Chord's brightest layer.
    int who = -1;
    if (_focus >= 0) who = _signals[_focus].ratio;
    else if (_chordFocus >= 0) who = _chord.ratio[_chordFocus];
    else if (_threat >= 0) who = _signals[_threat].ratio;
    else if (_chord.active) who = _chord.ratio[_chord.stripped];
    if (who >= 0) _targetHz = pitchOf(who);
    _targetLevel = _opt.notes && who >= 0 ? HUM_TARGET_LEVEL : 0;
    audio.setHum(1, _targetHz, _targetLevel);   // fading out at the pitch it had
    audio.setHiss(_static / 100.0f * HISS_LEVEL);
}

}  // namespace resonance

#include "ResonanceChord.h"
#include "ResonanceScope.h"
#include "ResonanceAutopilot.h"

using ResonanceFluxGame = resonance::ResonanceFluxGame;

#endif  // RESONANCE_FLUX_GAME_H
