#include "RollFluxGame.h"

namespace rollflux {

namespace {

// The optional sounds on the SD card: each falls back to another WAV, or
// a tone (or, for the checkpoint, a fall and the goal, a short melody)
// without it.
struct SfxDef { const char* path; const char* fallback; int hz, ms; };
const SfxDef ROLL_SFX[] = {
    { "/audio/roll_bump.wav",    nullptr,  160,  30 },   // SFX_BUMP
    { "/audio/pickup.wav",       nullptr, 1568,  50 },   // SFX_GEM (shared)
    { "/audio/roll_boost.wav",   nullptr, 1300,  60 },   // SFX_BOOST
    { "/audio/roll_charge.wav",  nullptr,  600,  90 },   // SFX_CHARGE
    { "/audio/roll_dash.wav",    nullptr, 1200, 120 },   // SFX_DASH
    { "/audio/roll_check.wav",   nullptr,    0,   0 },   // SFX_CHECK
    { "/audio/roll_fall.wav",    nullptr,    0,   0 },   // SFX_FALL
    { "/audio/roll_goal.wav",    nullptr,    0,   0 },   // SFX_GOAL
    { "/audio/roll_swap.wav",    nullptr, 1400,  40 },   // SFX_SWAP (high cyan, low magenta)
    { "/audio/roll_gate.wav",    nullptr,  220,  50 },   // SFX_GATE
    { "/audio/roll_crystal.wav", "/audio/explosion.wav", 0, 0 },   // SFX_CRYSTAL
    { "/audio/roll_bumper.wav",  nullptr,  990,  40 },   // SFX_BUMPER
    { "/audio/roll_boss_warn.wav", nullptr, 0,   0 },   // SFX_GUARD_WARN
    { "/audio/roll_boss_hit.wav", nullptr,  660,  90 },  // SFX_GUARD_HIT
    { "/audio/roll_boss_down.wav", "/audio/star_boss_die.wav", 0, 0 },   // SFX_GUARD_DOWN
    { "/audio/roll_slam.wav",    nullptr,  110,  80 },   // SFX_SLAM
};
static_assert(sizeof(ROLL_SFX) / sizeof(ROLL_SFX[0]) == 16, "one SfxDef per Sfx");

// While the demo runs, new sounds are dropped (lifted again whichever way
// update() returns).
struct RollSilence {
    AudioEngine &a; bool on;
    RollSilence(AudioEngine &a_, bool on_) : a(a_), on(on_) { if (on) a.setSilenced(true); }
    ~RollSilence() { if (on) a.setSilenced(false); }
};

}  // namespace

void RollFluxGame::init(AudioEngine &audio) {
    _audio = &audio;
    _scores.begin("roll");
    _lastFrameMs = millis();
    findSounds(audio);
    enterAttract();
    static const int n[] = { 392, 523, 659, 784 };
    static const int d[] = {  70,  70,  70, 160 };
    audio.playMelody(n, d, 4);
}

// Which optional sounds are on the card, checked once: a missing file
// would otherwise cost an SD open every time it's asked for. What will
// play is decoded into the mixer's cache now, so the first play isn't late.
void RollFluxGame::findSounds(AudioEngine &audio) {
    for (int i = 0; i < SFX_COUNT; ++i) {
        _sfxOnCard[i] = audio.exists(ROLL_SFX[i].path);
        if (_sfxOnCard[i]) audio.preload(ROLL_SFX[i].path);
        else if (ROLL_SFX[i].fallback) audio.preload(ROLL_SFX[i].fallback);
    }
    _musicOnCard = audio.exists(MUSIC);
}

void RollFluxGame::sfx(Sfx s) {
    if (_silent || !_audio) return;
    if (_sfxOnCard[s]) { _audio->playWAV(ROLL_SFX[s].path); return; }
    if (ROLL_SFX[s].fallback) { _audio->playWAV(ROLL_SFX[s].fallback); return; }
    switch (s) {
        case SFX_SWAP: _audio->playTone(_polarity == 0 ? 1400 : 700, 40); break;
        case SFX_GUARD_WARN: { static const int n[] = { 220, 0, 220, 0, 220 }, d[] = { 120, 60, 120, 60, 240 }; sfxMelody(n, d, 5); break; }
        case SFX_CHECK: { static const int n[] = { 880, 1175 }, d[] = { 60, 100 }; sfxMelody(n, d, 2); break; }
        case SFX_FALL:  { static const int n[] = { 700, 500, 330, 200 }, d[] = { 60, 60, 60, 120 }; sfxMelody(n, d, 4); break; }
        case SFX_GOAL:  { static const int n[] = { 523, 659, 784, 1047, 784, 1047 }, d[] = { 80, 80, 80, 120, 80, 240 };
                          sfxMelody(n, d, 6); break; }
        default: _audio->playTone(ROLL_SFX[s].hz, ROLL_SFX[s].ms); break;
    }
}

// Quitting (the cabinet's Back button): a game in progress still goes on
// the table (if it makes it), under the last name entered; a name being
// entered is kept. Nothing from the demo.
void RollFluxGame::onQuit(AudioEngine &audio) {
    if (_phase == PHASE_NAME) _scores.finishNow();
    else if (!_demo && (_phase == PHASE_PLAYING || _phase == PHASE_CLEAR)) _scores.record(_score);
    audio.mute();
}

// Movement is per second, scaled by the real frame time, so the ball rolls
// the same at 22 or 40fps.
void RollFluxGame::updateFrameScale() {
    unsigned long now = millis();
    unsigned long dt = now - _lastFrameMs;
    _lastFrameMs = now;
    if (dt < MIN_FRAME_MS) dt = MIN_FRAME_MS;
    if (dt > MAX_FRAME_MS) dt = MAX_FRAME_MS;
    _frameScale = (float)dt / (float)REFERENCE_FRAME_MS;
    _dt = dt / 1000.0f;
}

void RollFluxGame::startNewGame(AudioEngine &audio) {
    _demo = false;
    _silent = false;
    _scores.forget();
    _score = 0;
    _lives = START_LIVES;
    _gemsTotal = _goals = _falls = _dashes = 0;
    _crystals = _bumps = _swaps = 0;
    _dashGems = 0;
    _course = _loop = 0;
    _prevA = true;                    // the A that started it isn't a dash
    startCourse(audio);
    // Music plays during a game only: not on the attract screens or in the
    // demo, and it stops at game over.
    if (_musicOnCard) audio.loopWAV(MUSIC);
}

// A course from its text: cells, gems back in place, the start. The time
// limit shortens by 15% a loop round the courses, to 60% at most.
void RollFluxGame::loadCourse(int index) {
    const CourseDef &def = COURSES[index % COURSE_COUNT];
    _def = &def;
    _w = def.w;
    _h = def.h;
    _courseGems = 0;
    for (int r = 0; r < _h; ++r) {
        const char* row = def.rows[r];
        for (int c = 0; c < _w; ++c) {
            const char hc = row[c * 2], kc = row[c * 2 + 1];
            Cell &cell = _cells[r][c];
            cell.h = (hc >= '0' && hc <= '7') ? (uint8_t)(hc - '0') : 0;
            cell.flags = 0;
            switch (kc) {
                case '#': cell.kind = K_FLOOR; break;
                case 'R': cell.kind = K_FLOOR; cell.flags = F_RAIL; break;
                case '*': cell.kind = K_FLOOR; cell.flags = F_GEM; ++_courseGems; break;
                case 'S': cell.kind = K_START;
                          _startX = cellX0(c) + CELL * 0.5f;
                          _startZ = cellZ0(r) + CELL * 0.5f;
                          break;
                case 'G': cell.kind = K_GOAL; break;
                case 'n': cell.kind = K_RAMP_N; break;
                case 's': cell.kind = K_RAMP_S; break;
                case 'e': cell.kind = K_RAMP_E; break;
                case 'w': cell.kind = K_RAMP_W; break;
                case 'i': cell.kind = K_ICE; break;
                case 'I': cell.kind = K_ICE; cell.flags = F_RAIL; break;
                case 'C': cell.kind = K_CHECK; break;
                case '^': cell.kind = K_BOOST_N; break;
                case 'v': cell.kind = K_BOOST_S; break;
                case '>': cell.kind = K_BOOST_E; break;
                case '<': cell.kind = K_BOOST_W; break;
                case 'c': cell.kind = K_GATE_C; break;
                case 'm': cell.kind = K_GATE_M; break;
                case '(': cell.kind = K_BRIDGE_C; break;
                case ')': cell.kind = K_BRIDGE_M; break;
                case 'X': cell.kind = K_CRYSTAL; break;
                case 'o': cell.kind = K_BUMPER; break;
                case '8': cell.kind = K_CONV_N; break;
                case '2': cell.kind = K_CONV_S; break;
                case '6': cell.kind = K_CONV_E; break;
                case '4': cell.kind = K_CONV_W; break;
                case 'P': cell.kind = K_PISTON; break;
                case 'K': cell.kind = K_CORE; break;
                case 'O': cell.kind = K_RING; break;
                default:  cell.kind = K_VOID; break;
            }
        }
    }
    float scale = 1.0f - 0.15f * _loop;
    if (scale < 0.6f) scale = 0.6f;
    _courseMs = (long)(def.seconds * 1000L * scale);
    _courseGemsTaken = _courseFalls = 0;
    _respawnX = _startX;
    _respawnZ = _startZ;
    _respawnYaw = 0;
    _respawnTimeMs = _courseMs;
    _checkC = _checkR = -1;
    _yaw = 0;
    _rot[0] = _rot[4] = _rot[8] = 1;
    _rot[1] = _rot[2] = _rot[3] = _rot[5] = _rot[6] = _rot[7] = 0;
    _pathLen = 0;
    _targetC = _targetR = -1;
    _polarity = 0;                    // every course starts cyan
    _bumpUntil = _shardsUntil = 0;
    // Its moving parts, from their starting places.
    _moverCount = def.moverCount < MAX_MOVERS ? def.moverCount : MAX_MOVERS;
    _courseAt = millis();
    _onMover = -1;
    _pilotLink = -1;
    for (int k = 0; k < _moverCount; ++k) {
        Mover &mv = _movers[k];
        moverAt(def.movers[k], 0, mv.x, mv.z, mv.y, mv.ang);
        mv.px = mv.x; mv.pz = mv.z; mv.py = mv.y; mv.pang = mv.ang;
        mv.vx = mv.vz = 0;
    }
    _clearGuardian = 0;
    guardianSetup();
    respawn();
}

// The current course from the top: full time, every gem back.
void RollFluxGame::startCourse(AudioEngine &audio) {
    (void)audio;
    loadCourse(_course);
    _timeMs = _courseMs;
    _phase = PHASE_PLAYING;
    _phaseAt = millis();
    snprintf(_bannerBuf, sizeof(_bannerBuf), "%s %s", courseDef().code, courseDef().name);
    banner(_bannerBuf, guardianCourse() ? ArcadeConfig::COLOR_RED : ArcadeConfig::COLOR_CYAN, 2000);
    if (guardianCourse()) sfx(SFX_GUARD_WARN);
    else sfxTone(900, 80);
}

// Back at the start or the last checkpoint, still, facing the way it was.
void RollFluxGame::respawn() {
    _bx = _respawnX;
    _bz = _respawnZ;
    float y = 0;
    floorAt(_bx, _bz, y);
    _by = y;
    _vx = _vz = _vy = 0;
    _extraSpeed = _extraFade = 0;
    _falling = false;
    _fellOut = false;
    _charging = false;
    _dashUntil = 0;
    _dashBrakeDue = false;
    _braking = false;
    _camHold = false;
    _yaw = _respawnYaw;
    _leanRoll = _leanPitch = 0;
    _onMover = -1;
    _pilotLink = -1;
    _holdUntil = millis() + RESPAWN_HOLD_MS;
    updateCamera(true);
}

// A fall or the clock running out. Out of lives, it's game over (or the
// demo's end); out of time, the course starts again; a fall goes back to
// the last checkpoint with the time it had there.
void RollFluxGame::loseLife(AudioEngine &audio, const char* why) {
    --_lives;
    if (_lives <= 0) {
        _lives = 0;
        if (_demo) endDemo();
        else enterGameOver(audio);
        return;
    }
    if (why) {   // time up
        startCourse(audio);
        banner(why, ArcadeConfig::COLOR_RED, 2000);
        return;
    }
    _timeMs = _respawnTimeMs;
    respawn();
    banner(_checkC >= 0 ? "BACK TO CHECKPOINT" : "TRY AGAIN", ArcadeConfig::COLOR_ORANGE);
}

// The last ball's gone: a name for the table first, if the score made it.
void RollFluxGame::enterGameOver(AudioEngine &audio) {
    _phase = _scores.offer(_score) ? PHASE_NAME : PHASE_GAMEOVER;
    _phaseAt = millis();
    _charging = false;
    audio.stopLoop();
    static const int n[] = { 520, 390, 260, 130 };
    static const int d[] = { 120, 120, 120, 320 };
    sfxMelody(n, d, 4);
}

void RollFluxGame::banner(const char* text, uint16_t colour, unsigned long ms) {
    _banner = text;
    _bannerColour = colour;
    _bannerUntil = millis() + ms;
}

// The Flux Dash: press A with a step on the meter to charge (the ball
// held back and glowing, a line on the floor showing the way it'll go),
// let go to dash, by how long it charged. Let go too soon and nothing
// happens, the step kept.
void RollFluxGame::updateDash(const InputState &in) {
    const bool pressed = in.btnA && !_prevA;
    _prevA = in.btnA;
    if (!_charging) {
        if (pressed && dashSteps() > 0 && (long)(millis() - _holdUntil) >= 0) {
            _charging = true;
            _chargeAt = millis();
            sfx(SFX_CHARGE);
        }
        return;
    }
    float dx, dz;
    dashDirection(in, dx, dz);
    const float len = sqrtf(dx * dx + dz * dz) + 1e-6f;
    _aimX = dx / len;
    _aimZ = dz / len;
    if (in.btnA) return;
    _charging = false;
    const unsigned long held = millis() - _chargeAt;
    if (held < DASH_MIN_CHARGE_MS) return;
    const float t = held >= DASH_CHARGE_MS ? 1.0f : (float)held / DASH_CHARGE_MS;
    startDash(dx, dz, DASH_SPEED_MIN + (DASH_SPEED_MAX - DASH_SPEED_MIN) * t);
    _dashGems -= DASH_GEMS_PER_STEP;
    ++_dashes;
    sfx(SFX_DASH);
}

// B tapped, the stick left alone, swaps the ball's colour (on the
// release, so a B held for the camera doesn't); not again within the
// cooldown.
void RollFluxGame::updateSwap(const InputState &in) {
    const unsigned long now = millis();
    if (in.btnB && !_bDown) { _bDown = true; _bDownAt = now; _bStick = false; }
    if (in.btnB && (fabsf(in.joyX) > 0.3f || fabsf(in.joyY) > 0.3f)) _bStick = true;
    if (!in.btnB && _bDown) {
        _bDown = false;
        if (!_bStick && now - _bDownAt < SWAP_TAP_MS && (long)(now - _swapReadyAt) >= 0) swapColour();
    }
}

void RollFluxGame::swapColour() {
    _polarity ^= 1;
    _swapReadyAt = millis() + SWAP_COOLDOWN_MS;
    ++_swaps;
    sfx(SFX_SWAP);
}

// The way a dash goes: the stick's, or the ball's own with the stick
// centred (or the camera's, standing still).
void RollFluxGame::dashDirection(const InputState &in, float &dx, float &dz) const {
    stickToWorld(in, dx, dz);
    if (dx * dx + dz * dz < (0.3f * BALL_ACCEL) * (0.3f * BALL_ACCEL)) {
        if (_vx * _vx + _vz * _vz > 50.0f * 50.0f) { dx = _vx; dz = _vz; }
        else { dx = sinf(_yaw); dz = cosf(_yaw); }
    }
}

// The clock, falls, gems, checkpoints and the goal, after the ball's moved.
void RollFluxGame::stepRules(AudioEngine &audio) {
    if (_bump > 350.0f) sfx(SFX_BUMP);
    // A boost pad, on first rolling onto it.
    const int c = colAt(_bx), r = rowAt(_bz);
    const int here = solid(c, r) && !_falling && isBoost(_cells[r][c].kind) ? r * MAX_COURSE_W + c : -1;
    if (here >= 0 && here != _boostCell) sfx(SFX_BOOST);
    _boostCell = here;

    if (_fellOut) {
        ++_falls;
        ++_courseFalls;
        sfx(SFX_FALL);
        loseLife(audio, nullptr);
        return;
    }
    if ((long)(millis() - _holdUntil) >= 0) {
        _timeMs -= (long)(_dt * 1000.0f + 0.5f);
        if (_timeMs <= 0) {
            _timeMs = 0;
            sfxTone(200, 400);
            loseLife(audio, "TIME UP");
            return;
        }
        // The last seconds tick.
        if (_timeMs < (long)TIME_WARN_MS && millis() - _tickAt >= 1000) {
            _tickAt = millis();
            sfxTone(1800, 25);
        }
    }
    collectGems(audio);
    if (_falling || !solid(c, r)) return;
    const Cell &cell = _cells[r][c];
    if (cell.kind == K_CHECK && (c != _checkC || r != _checkR)) {
        // A row of checkpoint cells is one checkpoint: only a new row says so.
        if (_checkR != r) {
            banner("CHECKPOINT", ArcadeConfig::COLOR_CYAN);
            sfx(SFX_CHECK);
        }
        _checkC = c;
        _checkR = r;
        _respawnX = cellX0(c) + CELL * 0.5f;
        _respawnZ = cellZ0(r) + CELL * 0.5f;
        _respawnYaw = _yaw;
        _respawnTimeMs = _timeMs;
    }
    if (cell.kind == K_GOAL) reachGoal(audio);
}

// A gem's taken when the ball's within reach of it, in the air or not:
// points, time, and a gem on the dash meter.
void RollFluxGame::collectGems(AudioEngine &audio) {
    (void)audio;
    const int c0 = colAt(_bx), r0 = rowAt(_bz);
    for (int r = r0 - 1; r <= r0 + 1; ++r)
        for (int c = c0 - 1; c <= c0 + 1; ++c) {
            if (!solid(c, r)) continue;
            Cell &cell = _cells[r][c];
            if ((cell.flags & (F_GEM | F_TAKEN)) != F_GEM) continue;
            const float dx = cellX0(c) + CELL * 0.5f - _bx, dz = cellZ0(r) + CELL * 0.5f - _bz;
            const float dy = gemY(c, r) - (_by + BALL_RADIUS);
            if (dx * dx + dz * dz + dy * dy > (BALL_RADIUS + 40.0f) * (BALL_RADIUS + 40.0f)) continue;
            cell.flags |= F_TAKEN;
            ++_courseGemsTaken;
            ++_gemsTotal;
            // A guardian's arena grows its gems back.
            if (guardianCourse() && _regrowCount < 12)
                _regrow[_regrowCount++] = Regrow{ (int8_t)c, (int8_t)r, millis() + GEM_REGROW_MS };
            _score += GEM_POINTS;
            _timeMs += GEM_TIME_MS;
            const int before = dashSteps();
            if (_dashGems < DASH_STEPS * DASH_GEMS_PER_STEP) ++_dashGems;
            if (_gemsTotal % GEMS_PER_LIFE == 0 && _lives < MAX_LIVES) {
                ++_lives;
                banner("EXTRA BALL", ArcadeConfig::COLOR_GREEN);
            } else if (dashSteps() > before) {
                banner("DASH READY: HOLD A", ArcadeConfig::COLOR_CYAN);
            }
            sfx(SFX_GEM);
        }
}

// The tally: a hundred a second left, a bonus for no falls and another for
// every gem (and one for a guardian beaten); then the next course.
void RollFluxGame::reachGoal(AudioEngine &audio) {
    (void)audio;
    ++_goals;
    _clearTime = (_timeMs / 1000) * TIME_POINTS;
    _clearNoFall = _courseFalls == 0 ? NO_FALL_BONUS : 0;
    _clearAllGems = _courseGems > 0 && _courseGemsTaken == _courseGems ? ALL_GEMS_BONUS : 0;
    _score += _clearTime + _clearNoFall + _clearAllGems + _clearGuardian;
    _phase = PHASE_CLEAR;
    _phaseAt = millis();
    _charging = false;
    sfx(SFX_GOAL);
}

// The course leans with the stick, eased, so the tilt shows.
void RollFluxGame::updateLean(const InputState &in) {
    const float k = fminf(1.0f, LEAN_EASE * _dt);
    _leanRoll += (in.joyY * LEAN_ROLL - _leanRoll) * k;
    _leanPitch += (-in.joyX * LEAN_PITCH - _leanPitch) * k;
}

// Behind and above the ball, turning slowly to the way it's rolling (not
// while it's nearly still, falling, or rolling back towards the camera,
// down a ramp say, when it holds its heading so the stick keeps its
// sense; nor while it's been turned by hand, until the ball sets off a
// new way), and looking a little ahead of it. While the ball falls the
// camera stays put, watching it go.
void RollFluxGame::updateCamera(bool snap) {
    if (!(_falling && !snap)) {
        const float speed = sqrtf(_vx * _vx + _vz * _vz);
        if (_camHold && speed > YAW_FOLLOW_SPEED) {
            const float heading = atan2f(_vx, _vz);
            if (!_camHoldMoving) { _camHoldMoving = true; _camHoldDir = heading; }
            float off = heading - _camHoldDir;
            while (off > (float)PI) off -= 2.0f * (float)PI;
            while (off < -(float)PI) off += 2.0f * (float)PI;
            if (fabsf(off) > CAMERA_HOLD_TURN) _camHold = false;
        }
        if (speed > YAW_FOLLOW_SPEED && !snap && !_camHold) {
            float diff = atan2f(_vx, _vz) - _yaw;
            while (diff > (float)PI) diff -= 2.0f * (float)PI;
            while (diff < -(float)PI) diff += 2.0f * (float)PI;
            if (fabsf(diff) < YAW_FOLLOW_MAX) _yaw += diff * fminf(1.0f, CAMERA_YAW_EASE * _dt);
        }
        const float fx = sinf(_yaw), fz = cosf(_yaw);
        const float tx = _bx - fx * CAMERA_BACK, ty = _by + CAMERA_UP, tz = _bz - fz * CAMERA_BACK;
        const float k = snap ? 1.0f : fminf(1.0f, CAMERA_POS_EASE * _dt);
        _camX += (tx - _camX) * k;
        _camY += (ty - _camY) * k;
        _camZ += (tz - _camZ) * k;
    }
    const float fx = sinf(_yaw), fz = cosf(_yaw);
    const float lx = _bx + fx * CAMERA_LOOK_AHEAD - _camX;
    const float ly = _by - _camY;
    const float lz = _bz + fz * CAMERA_LOOK_AHEAD - _camZ;
    _camera.setPosition((int32_t)lroundf(_camX), (int32_t)lroundf(_camY), (int32_t)lroundf(_camZ));
    _camera.rotation.x = -atan2f(ly, sqrtf(lx * lx + lz * lz)) - _leanPitch;
    _camera.rotation.y = atan2f(lx, lz);
    _camera.rotation.z = _leanRoll;
}

bool RollFluxGame::update(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    ensureReady(canvas);
    updateFrameScale();
    // Quitting is the cabinet's Back button (main.cpp, then onQuit()).
    if (_demo) {
        // The demo: the autopilot plays, silently, until A, its time, or
        // its last ball.
        if (input.btnAPressed) {
            startNewGame(audio);
            return updatePlaying(canvas, InputState{}, audio);
        }
        RollSilence quiet(audio, true);
        _silent = true;
        const InputState in = pilot(true);
        if (_phase == PHASE_CLEAR) updateClear(canvas, in, audio);
        else updatePlaying(canvas, in, audio);
        if (_demo && (long)(millis() - _demoUntil) >= 0) endDemo();
        return true;
    }
    switch (_phase) {
        case PHASE_ATTRACT:  return updateAttract(canvas, input, audio);
        case PHASE_CLEAR:    return updateClear(canvas, input, audio);
        case PHASE_NAME:     return updateName(canvas, input, audio);
        case PHASE_GAMEOVER: return updateGameOver(canvas, input, audio);
        default:             return updatePlaying(canvas, input, audio);
    }
}

bool RollFluxGame::updatePlaying(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    updateSwap(input);
    // B held: the stick turns the camera instead of the course.
    InputState in = input;
    if (input.btnB) {
        _yaw += input.joyY * CAMERA_TURN_SPEED * _dt;
        if (_yaw > (float)PI) _yaw -= 2.0f * (float)PI;
        if (_yaw < -(float)PI) _yaw += 2.0f * (float)PI;
        in.joyX = in.joyY = 0;
        _camHold = true;
        _camHoldMoving = false;
    }
    updateDash(in);
    updateMovers();
    updateGuardian();
    carryBall();
    stepBall(in);
    findOnMover();
    guardianHits();
    updateLean(in);
    updateCamera(false);
    stepRules(audio);
    if (_phase == PHASE_ATTRACT) {       // the demo's last ball: back to the title
        updateAttract(canvas, InputState{}, audio);
        return true;
    }
    const unsigned long t0 = micros();
    renderFrame(canvas);
    _renderUs = micros() - t0;
    if (_phase == PHASE_NAME) _scores.draw(canvas);
    else if (_phase == PHASE_GAMEOVER) renderGameOver(canvas);
    else drawHUD(canvas);
    return true;
}

// The ball rolls to a stop on the goal under the tally; then the next
// course (A skips the wait).
bool RollFluxGame::updateClear(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    _prevA = input.btnA;
    updateMovers();
    carryBall();
    stepBall(InputState{});
    findOnMover();
    _leanRoll *= 0.9f;
    _leanPitch *= 0.9f;
    updateCamera(false);
    renderFrame(canvas);
    drawHUD(canvas);
    renderClear(canvas);
    const unsigned long t = millis() - _phaseAt;
    if (t > CLEAR_MS || (t > ArcadeConfig::GAMEOVER_INPUT_DELAY_MS && input.btnAPressed)) {
        ++_course;
        if (_course % COURSE_COUNT == 0) ++_loop;
        _prevA = true;
        startCourse(audio);
    }
    return true;
}

// The course goes on behind the name entry; when it's done (or timed
// out), the game-over screen.
bool RollFluxGame::updateName(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    updateMovers();
    if (!_fellOut) { carryBall(); stepBall(InputState{}); findOnMover(); }
    renderFrame(canvas);
    _scores.draw(canvas);
    if (_scores.update(input, getRotation())) {
        _phase = PHASE_GAMEOVER;
        _phaseAt = millis();
        audio.playTone(1047, 80);
    }
    return true;
}

bool RollFluxGame::updateGameOver(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    updateMovers();
    if (!_fellOut) { carryBall(); stepBall(InputState{}); findOnMover(); }
    renderFrame(canvas);
    renderGameOver(canvas);
    const unsigned long t = millis() - _phaseAt;
    if (t > ArcadeConfig::GAMEOVER_INPUT_DELAY_MS && input.btnAPressed) startNewGame(audio);
    else if (t > GAMEOVER_TIMEOUT_MS) enterAttract();
    return true;
}

}  // namespace rollflux
