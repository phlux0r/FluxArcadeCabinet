#include "RollFluxGame.h"

namespace rollflux {

void RollFluxGame::init(AudioEngine &audio) {
    _lastFrameMs = millis();
    static const int n[] = { 392, 523, 659, 784 };
    static const int d[] = {  70,  70,  70, 160 };
    audio.playMelody(n, d, 4);
    _phase = PHASE_PLAYING;
    _score = 0;
    _lives = START_LIVES;
    _gemsTotal = _goals = _falls = 0;
    _course = _loop = 0;
    loadCourse(0);
    _timeMs = _courseMs;
    banner(COURSES[0].name, ArcadeConfig::COLOR_CYAN, 2000);
}

// Quitting (the cabinet's Back button).
void RollFluxGame::onQuit(AudioEngine &audio) {
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
    _score = 0;
    _lives = START_LIVES;
    _gemsTotal = _goals = _falls = 0;
    _course = _loop = 0;
    startCourse(audio);
}

// A course from its text: cells, gems back in place, the start. The time
// limit shortens by 15% a loop round the courses, to 60% at most.
void RollFluxGame::loadCourse(int index) {
    const CourseDef &def = COURSES[index % COURSE_COUNT];
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
                case 'C': cell.kind = K_CHECK; break;
                case '^': cell.kind = K_BOOST_N; break;
                case 'v': cell.kind = K_BOOST_S; break;
                case '>': cell.kind = K_BOOST_E; break;
                case '<': cell.kind = K_BOOST_W; break;
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
    respawn();
}

// The current course from the top: full time, every gem back.
void RollFluxGame::startCourse(AudioEngine &audio) {
    loadCourse(_course);
    _timeMs = _courseMs;
    _phase = PHASE_PLAYING;
    _phaseAt = millis();
    banner(COURSES[_course % COURSE_COUNT].name, ArcadeConfig::COLOR_CYAN, 2000);
    sfxTone(audio, 900, 80);
}

// Back at the start or the last checkpoint, still, facing the way it was.
void RollFluxGame::respawn() {
    _bx = _respawnX;
    _bz = _respawnZ;
    float y = 0;
    floorAt(_bx, _bz, y);
    _by = y;
    _vx = _vz = _vy = 0;
    _extraSpeed = 0;
    _falling = false;
    _fellOut = false;
    _yaw = _respawnYaw;
    _leanRoll = _leanPitch = 0;
    _holdUntil = millis() + RESPAWN_HOLD_MS;
    updateCamera(true);
}

// A fall or the clock running out. Out of lives, it's game over; out of
// time, the course starts again; a fall goes back to the last checkpoint
// with the time it had there.
void RollFluxGame::loseLife(AudioEngine &audio, const char* why) {
    --_lives;
    if (_lives <= 0) {
        _lives = 0;
        _phase = PHASE_GAMEOVER;
        _phaseAt = millis();
        static const int n[] = { 520, 390, 260, 130 };
        static const int d[] = { 120, 120, 120, 320 };
        sfxMelody(audio, n, d, 4);
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

void RollFluxGame::banner(const char* text, uint16_t colour, unsigned long ms) {
    _banner = text;
    _bannerColour = colour;
    _bannerUntil = millis() + ms;
}

// The clock, falls, gems, checkpoints and the goal, after the ball's moved.
void RollFluxGame::stepRules(AudioEngine &audio) {
    if (_bump > 350.0f) sfxTone(audio, 140 + (int)(_bump * 0.1f), 30);
    // A boost pad, on first rolling onto it.
    const int c = colAt(_bx), r = rowAt(_bz);
    const int here = solid(c, r) && !_falling && isBoost(_cells[r][c].kind) ? r * MAX_COURSE_W + c : -1;
    if (here >= 0 && here != _boostCell) sfxTone(audio, 1300, 60);
    _boostCell = here;

    if (_fellOut) {
        ++_falls;
        ++_courseFalls;
        static const int n[] = { 700, 500, 330, 200 };
        static const int d[] = {  60,  60,  60, 120 };
        sfxMelody(audio, n, d, 4);
        loseLife(audio, nullptr);
        return;
    }
    if ((long)(millis() - _holdUntil) >= 0) {
        _timeMs -= (long)(_dt * 1000.0f + 0.5f);
        if (_timeMs <= 0) {
            _timeMs = 0;
            sfxTone(audio, 200, 400);
            loseLife(audio, "TIME UP");
            return;
        }
        // The last seconds tick.
        if (_timeMs < (long)TIME_WARN_MS && millis() - _tickAt >= 1000) {
            _tickAt = millis();
            sfxTone(audio, 1800, 25);
        }
    }
    collectGems(audio);
    if (_falling || !solid(c, r)) return;
    const Cell &cell = _cells[r][c];
    if (cell.kind == K_CHECK && (c != _checkC || r != _checkR)) {
        // A row of checkpoint cells is one checkpoint: only a new row says so.
        if (_checkR != r) {
            banner("CHECKPOINT", ArcadeConfig::COLOR_CYAN);
            static const int n[] = { 880, 1175 };
            static const int d[] = {  60,  100 };
            sfxMelody(audio, n, d, 2);
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

// A gem's taken when the ball's within reach of it, in the air or not.
void RollFluxGame::collectGems(AudioEngine &audio) {
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
            _score += GEM_POINTS;
            _timeMs += GEM_TIME_MS;
            if (_gemsTotal % GEMS_PER_LIFE == 0 && _lives < MAX_LIVES) {
                ++_lives;
                banner("EXTRA BALL", ArcadeConfig::COLOR_GREEN);
            }
            sfxTone(audio, 1568, 50);
        }
}

// The tally: a hundred a second left, a bonus for no falls and another for
// every gem; then the next course.
void RollFluxGame::reachGoal(AudioEngine &audio) {
    ++_goals;
    _clearTime = (_timeMs / 1000) * TIME_POINTS;
    _clearNoFall = _courseFalls == 0 ? NO_FALL_BONUS : 0;
    _clearAllGems = _courseGems > 0 && _courseGemsTaken == _courseGems ? ALL_GEMS_BONUS : 0;
    _score += _clearTime + _clearNoFall + _clearAllGems;
    _phase = PHASE_CLEAR;
    _phaseAt = millis();
    static const int n[] = { 523, 659, 784, 1047, 784, 1047 };
    static const int d[] = {  80,  80,  80,  120,  80,  240 };
    sfxMelody(audio, n, d, 6);
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
// sense), and
// looking a little ahead of it. While the ball falls the camera stays put,
// watching it go.
void RollFluxGame::updateCamera(bool snap) {
    if (!(_falling && !snap)) {
        const float speed = sqrtf(_vx * _vx + _vz * _vz);
        if (speed > YAW_FOLLOW_SPEED && !snap) {
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
    switch (_phase) {
        case PHASE_CLEAR:    return updateClear(canvas, input, audio);
        case PHASE_GAMEOVER: return updateGameOver(canvas, input, audio);
        default:             return updatePlaying(canvas, input, audio);
    }
}

bool RollFluxGame::updatePlaying(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    stepBall(input);
    updateLean(input);
    updateCamera(false);
    stepRules(audio);
    const unsigned long t0 = micros();
    renderFrame(canvas);
    _renderUs = micros() - t0;
    if (_phase == PHASE_GAMEOVER) renderGameOver(canvas);
    else drawHUD(canvas);
    return true;
}

// The ball rolls to a stop on the goal under the tally; then the next
// course (A skips the wait).
bool RollFluxGame::updateClear(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    stepBall(InputState{});
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
        startCourse(audio);
    }
    return true;
}

bool RollFluxGame::updateGameOver(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    if (!_fellOut) stepBall(InputState{});
    renderFrame(canvas);
    renderGameOver(canvas);
    const unsigned long t = millis() - _phaseAt;
    if (t > ArcadeConfig::GAMEOVER_INPUT_DELAY_MS && input.btnAPressed) startNewGame(audio);
    return true;
}

}  // namespace rollflux
