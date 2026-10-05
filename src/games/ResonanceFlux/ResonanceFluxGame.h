#ifndef RESONANCE_FLUX_GAME_H
#define RESONANCE_FLUX_GAME_H

#include <stdlib.h>
#include <string.h>
#include <esp_heap_caps.h>
#include "../../games/IGame.h"
#include "../../cabinet/ArcadeConfig.h"
#include "../../cabinet/AudioEngine.h"
#include "../../cabinet/HighScores.h"
#include "ResonanceConfig.h"
#include "ResonanceFigure.h"

// =============================================================================
// RESONANCE FLUX (prototype): a green phosphor oscilloscope, landscape.
// The stick tunes your Lissajous figure: left/right steps the dial round
// its ring of ratios, up/down turns the phase. Signals weave in towards
// it, each a figure of its own. On a signal's ratio with its phase
// matched, both glow: A shatters it. Signals that reach you add static
// (noise on the scope, hiss, a wander in your phase), and at 100 static
// the game's over. B dampens: everything slows for a while. You hear your
// ratio's note and a signal's, the same note when you're on its ratio
// (the mixer's hum, audio/AudioMixer.h).
//
// The prototype is for judging the idea on the board: one kind of signal,
// no cascades, chains or Chords, no attract demo. docs/design/
// ResonanceFlux.md has the full design.
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
    enum Phase : uint8_t { PHASE_TITLE, PHASE_PLAYING, PHASE_NAME, PHASE_GAMEOVER };
    // Within a game: the wave's number showing, play, and the wave's tally.
    enum Round : uint8_t { ROUND_INTRO, ROUND_PLAY, ROUND_CLEAR };

    struct Signal {
        bool alive = false;
        uint8_t ratio = 0;            // into RATIOS
        float phase = 0;
        float x = 0, y = 0;
        float speed = 0;              // px/s along its weaving course
        float sway = 0, swayRate = 0, swayAt = 0;   // radians, radians/s, where in the sway
    };
    struct Shard { float x = 0, y = 0, vx = 0, vy = 0; unsigned long until = 0; bool white = false; };

    // ---- Phases and play (this file) ----
    void startGame(AudioEngine &audio);
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
    int  aliveCount() const;
    int  maxOnScope() const;
    float driftSpeed() const;
    static float curve(float v);
    static float frand(float lo, float hi);
    void tone(int hz, int ms) { if (_audio) _audio->playTone(hz, ms); }

    // ---- The scope and drawing (ResonanceScope.h) ----
    bool allocGlow();
    void freeGlow();
    void buildLut();
    void fadeGlow();
    void plot(uint8_t *plane, int x, int y, uint8_t v);
    void line(uint8_t *plane, int x0, int y0, int x1, int y1, uint8_t v);
    void drawFigure(uint8_t *a, uint8_t *b, const Ratio &r, float phase, float cx, float cy, float rad, bool big);
    void pushGlow(GFXcanvas16 &cv);
    void renderPlay(GFXcanvas16 &cv);
    void renderTitle(GFXcanvas16 &cv);
    void renderGameOver(GFXcanvas16 &cv);
    void drawHud(GFXcanvas16 &cv);
    void drawStrip(GFXcanvas16 &cv);
    void drawOverlays(GFXcanvas16 &cv);

    // ---- The harness's player (ResonanceAutopilot.h) ----
    InputState autopilot();

    // ---- State ----
    hiscore::ScoreBoard _scores;
    Signal _signals[MAX_SIGNALS];
    Shard  _shards[MAX_SHARDS];
    uint8_t *_glow = nullptr;            // two planes, green then amber, W x SCOPE_H each
    uint16_t _lut[32 * 32];              // (green, amber) -> colour

    Phase _phaseState = PHASE_TITLE;
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
    bool  _bang = false;                 // explosion.wav on the card
    float _targetHz = 400;               // the focus signal's tone

    // Title: the figure it morphs through.
    float _titleAt = 0, _titlePhase = 0;    // the title's ratio (in RATIOS, as it climbs) and phase

    // Autopilot: A, B and a dial step held last frame.
    bool _apPrevA = false, _apPrevB = false, _apPrevStep = false;

    // Running totals, for the host harness (test/resonanceflux_harness.cpp).
    long _statShatters = 0, _statMisfires = 0, _statHits = 0, _statWaves = 0, _statDampens = 0;

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

inline void ResonanceFluxGame::init(AudioEngine &audio) {
    _audio = &audio;
    _scores.begin("resonance");
    allocGlow();
    buildLut();
    _bang = audio.exists("/audio/explosion.wav");
    if (_bang) audio.preload("/audio/explosion.wav");
    _lastFrameMs = millis();
    _phaseState = PHASE_TITLE;
    _phaseAt = millis();
    for (auto &s : _signals) s.alive = false;
    for (auto &s : _shards) s.until = 0;
    static const int n[] = { 330, 495, 660, 990 };
    static const int d[] = {  90,  90,  90, 220 };
    audio.playMelody(n, d, 4);
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
        case PHASE_TITLE:
            audio.stopHum();
            if (input.btnAPressed) { startGame(audio); renderPlay(canvas); return true; }
            renderTitle(canvas);
            return true;
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
            if (t > GAMEOVER_MIN_MS && input.btnAPressed) startGame(audio);
            else if (t > GAMEOVER_TIMEOUT_MS) { _phaseState = PHASE_TITLE; _phaseAt = _now; }
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

// Quitting (the Back button): a game in progress still goes on the table;
// a name being entered is kept.
inline void ResonanceFluxGame::onQuit(AudioEngine &audio) {
    if (_phaseState == PHASE_NAME) _scores.finishNow();
    else if (_phaseState == PHASE_PLAYING) _scores.record(_score);
    audio.mute();
}

inline void ResonanceFluxGame::startGame(AudioEngine &audio) {
    _phaseState = PHASE_PLAYING;
    _phaseAt = _now;
    _score = 0;
    _static = 0;
    _stopCount = 0;
    _stop = 0;
    _phase = 0;
    _jitP = _jitTP = 0;
    _stepDir = 0;
    _scores.forget();
    for (auto &s : _shards) s.until = 0;
    startWave(1);
    audio.playTone(900, 80);
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
    _dampens = DAMPEN_PER_WAVE;
    _dampUntil = 0;
    for (auto &s : _signals) s.alive = false;
    _focus = _matched = _threat = -1;
    _round = ROUND_INTRO;
    _roundAt = _now;
}

inline void ResonanceFluxGame::enterGameOver(AudioEngine &audio) {
    _phaseState = _scores.offer(_score) ? PHASE_NAME : PHASE_GAMEOVER;
    _phaseAt = _now;
    _static = 100;
    audio.stopHum();
    static const int n[] = { 440, 415, 392, 220 };
    static const int d[] = { 140, 140, 140, 400 };
    audio.playMelody(n, d, 4);
}

inline int ResonanceFluxGame::aliveCount() const {
    int n = 0;
    for (auto &s : _signals) n += s.alive;
    return n;
}

inline int ResonanceFluxGame::maxOnScope() const {
    const int m = ON_SCOPE_FIRST + (_wave - 1) / ON_SCOPE_EVERY;
    return m < MAX_SIGNALS ? m : MAX_SIGNALS;
}

inline float ResonanceFluxGame::driftSpeed() const {
    const float s = DRIFT_START * powf(DRIFT_GROWTH, (float)(_wave - 1));
    return s < DRIFT_MAX ? s : DRIFT_MAX;
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
        if (_now - _roundAt >= WAVE_INTRO_MS) { _round = ROUND_PLAY; _roundAt = _now; _spawnAt = _now; }
        return;
    }
    if (_round == ROUND_CLEAR) {
        if (_now - _roundAt >= WAVE_CLEAR_MS) startWave(_wave + 1);
        return;
    }

    if (_toSpawn > 0 && aliveCount() < maxOnScope() && (long)(_now - _spawnAt) >= 0) {
        spawnSignal();
        const long step = (long)SPAWN_FIRST_MS - (long)SPAWN_STEP_MS * (_wave - 1);
        _spawnAt = _now + (unsigned long)(step > (long)SPAWN_MIN_MS ? step : (long)SPAWN_MIN_MS);
    }
    moveSignals();
    findMatch();
    if (in.btnAPressed) fire();
    if (in.btnBPressed && _dampens > 0 && _now >= _dampUntil) {
        --_dampens;
        _dampUntil = _now + DAMPEN_MS;
        ++_statDampens;
        tone(220, 200);
    }
    if (_toSpawn == 0 && aliveCount() == 0) {
        _clearBonus = PTS_STATIC_LEFT * (long)(100.0f - _static) + PTS_DAMPEN_LEFT * _dampens;
        _score += _clearBonus;
        addStatic(STATIC_CLEAR);
        ++_statWaves;
        _round = ROUND_CLEAR;
        _roundAt = _now;
        static const int n[] = { 660, 880, 1320 };
        static const int d[] = {  80,  80,  200 };
        if (_audio) _audio->playMelody(n, d, 3);
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
    --_toSpawn;
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
    if (_matched >= 0) { shatter(_matched); return; }
    ++_statMisfires;
    addStatic(STATIC_MISFIRE);
    _fireReadyAt = _now + FIRE_COOLDOWN_MS;
    tone(140, 90);
}

// A shot in resonance: the signal's figure flies apart, worth up to double
// the further out it was.
inline void ResonanceFluxGame::shatter(int i) {
    Signal &s = _signals[i];
    const Ratio &r = RATIOS[s.ratio];
    const float dx = s.x - CORE_X, dy = s.y - CORE_Y;
    float far = (sqrtf(dx * dx + dy * dy) - CORE_R) / (70.0f - CORE_R);
    far = far < 0 ? 0 : far > 1 ? 1 : far;
    _score += (long)(PTS_TONE * (1.0f + far));
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
    if (_bang && _audio) _audio->playWAV("/audio/explosion.wav");
    else tone(1400, 60);
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
    tone(90, 160);
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
    audio.setHum(0, pitchOf(_stops[_stop]), HUM_YOU_LEVEL);
    const int who = _focus >= 0 ? _focus : _threat;
    if (who >= 0) _targetHz = pitchOf(_signals[who].ratio);
    audio.setHum(1, _targetHz, who >= 0 ? HUM_TARGET_LEVEL : 0);   // fading out at the pitch it had
    audio.setHiss(_static / 100.0f * HISS_LEVEL);
}

}  // namespace resonance

#include "ResonanceScope.h"
#include "ResonanceAutopilot.h"

using ResonanceFluxGame = resonance::ResonanceFluxGame;

#endif  // RESONANCE_FLUX_GAME_H
