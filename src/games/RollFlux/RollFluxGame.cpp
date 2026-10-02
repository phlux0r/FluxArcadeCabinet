#include "RollFluxGame.h"

namespace rollflux {

void RollFluxGame::init(AudioEngine &audio) {
    _lastFrameMs = millis();
    _direct = true;
    _preset = 1;
    _goals = _falls = 0;
    loadCourse(0);
    static const int n[] = { 392, 523, 659, 784 };
    static const int d[] = {  70,  70,  70, 160 };
    audio.playMelody(n, d, 4);
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

void RollFluxGame::loadCourse(int index) {
    const CourseDef &def = COURSES[index % COURSE_COUNT];
    _w = def.w;
    _h = def.h;
    for (int r = 0; r < _h; ++r) {
        const char* row = def.rows[r];
        for (int c = 0; c < _w; ++c) {
            const char hc = row[c * 2], kc = row[c * 2 + 1];
            Cell &cell = _cells[r][c];
            cell.h = (hc >= '0' && hc <= '7') ? (uint8_t)(hc - '0') : 0;
            switch (kc) {
                case '#': cell.kind = K_FLOOR; break;
                case 'S': cell.kind = K_START;
                          _startX = cellX0(c) + CELL * 0.5f;
                          _startZ = cellZ0(r) + CELL * 0.5f;
                          break;
                case 'G': cell.kind = K_GOAL; break;
                case 'n': cell.kind = K_RAMP_N; break;
                case 's': cell.kind = K_RAMP_S; break;
                case 'e': cell.kind = K_RAMP_E; break;
                case 'w': cell.kind = K_RAMP_W; break;
                default:  cell.kind = K_VOID; break;
            }
        }
    }
    if (_scene) buildChunks();
    _yaw = 0;
    respawn();
}

void RollFluxGame::respawn() {
    _bx = _startX;
    _bz = _startZ;
    float y = 0;
    floorAt(_bx, _bz, y);
    _by = y;
    _vx = _vz = _vy = 0;
    _falling = false;
    updateCamera(true);
}

// The floor's height under (x, z), or false over the void. Ramps rise
// across their cell from their low edge.
bool RollFluxGame::floorAt(float x, float z, float &y) const {
    if (x < 0 || z < 0) return false;
    const int c = (int)(x / CELL), rs = (int)(z / CELL);   // rs counts from the south
    const int r = _h - 1 - rs;
    if (!solid(c, r)) return false;
    const Cell &cell = _cells[r][c];
    const float fx = (x - cellX0(c)) / CELL, fz = (z - cellZ0(r)) / CELL;
    float base = (float)(cell.h * HEIGHT_STEP);
    switch (cell.kind) {
        case K_RAMP_N: base += HEIGHT_STEP * fz; break;
        case K_RAMP_S: base += HEIGHT_STEP * (1.0f - fz); break;
        case K_RAMP_E: base += HEIGHT_STEP * fx; break;
        case K_RAMP_W: base += HEIGHT_STEP * (1.0f - fx); break;
        default: break;
    }
    y = base;
    return true;
}

// The stick, camera-relative: screen up rolls the ball away from the
// camera. Landscape: screen up is -joyX, screen right +joyY.
void RollFluxGame::stickToWorld(const InputState &in, float &ax, float &az) const {
    float up = -in.joyX, right = in.joyY;
    const float m = sqrtf(up * up + right * right);
    if (m > 1.0f) { up /= m; right /= m; }
    const float fx = sinf(_yaw), fz = cosf(_yaw);    // forward
    const float rx = cosf(_yaw), rz = -sinf(_yaw);   // right
    ax = (up * fx + right * rx) * BALL_ACCEL;
    az = (up * fz + right * rz) * BALL_ACCEL;
}

// The ball: the stick's push and a ramp's slope accelerate it, friction
// slows it, and it moves in steps of at most SUBSTEP, across then along,
// so it can't pass through a step at any speed. A rise of more than
// STEP_UP is a wall; a drop of more than STEP_DOWN, or the void, starts a
// fall. Fallen FALL_DEPTH below the course, it's back to the start.
void RollFluxGame::stepBall(const InputState &in) {
    const float dt = _dt;
    float ax, az;
    stickToWorld(in, ax, az);
    if (_falling) { ax *= 0.3f; az *= 0.3f; }        // a little steering in the air
    else {
        float gy;
        const int c = (int)(_bx / CELL), r = _h - 1 - (int)(_bz / CELL);
        if (floorAt(_bx, _bz, gy) && solid(c, r)) {
            switch (_cells[r][c].kind) {
                case K_RAMP_N: az -= SLOPE_GRAVITY; break;
                case K_RAMP_S: az += SLOPE_GRAVITY; break;
                case K_RAMP_E: ax -= SLOPE_GRAVITY; break;
                case K_RAMP_W: ax += SLOPE_GRAVITY; break;
                default: break;
            }
        }
    }
    _vx += ax * dt;
    _vz += az * dt;
    if (!_falling) {
        const float keep = 1.0f - BALL_FRICTION * dt;
        _vx *= keep > 0 ? keep : 0;
        _vz *= keep > 0 ? keep : 0;
    }
    const float speed = sqrtf(_vx * _vx + _vz * _vz);
    if (speed > BALL_MAX_SPEED) { _vx *= BALL_MAX_SPEED / speed; _vz *= BALL_MAX_SPEED / speed; }

    const float dist = speed * dt;
    int n = (int)ceilf(dist / SUBSTEP);
    if (n < 1) n = 1;
    const float sdt = dt / n;
    for (int i = 0; i < n; ++i) {
        float fy;
        // Across, then along: a rise too big to roll up turns the ball.
        const float nx = _bx + _vx * sdt;
        if (floorAt(nx, _bz, fy) && fy > _by + STEP_UP) _vx = -_vx * WALL_BOUNCE;
        else _bx = nx;
        const float nz = _bz + _vz * sdt;
        if (floorAt(_bx, nz, fy) && fy > _by + STEP_UP) _vz = -_vz * WALL_BOUNCE;
        else _bz = nz;
        // Down: on the floor it follows it; off it (or over a drop), it falls.
        float gy;
        const bool ground = floorAt(_bx, _bz, gy);
        if (!_falling) {
            if (!ground || gy < _by - STEP_DOWN) { _falling = true; _vy = 0; }
            else _by = gy;
        }
        if (_falling) {
            _vy -= GRAVITY * sdt;
            _by += _vy * sdt;
            // Landing: onto the floor from above, not from under its edge.
            if (ground && _by <= gy && gy - _by < 40.0f) { _by = gy; _vy = 0; _falling = false; }
        }
    }

    if (_by < -FALL_DEPTH) { ++_falls; respawn(); return; }
    if (!_falling) {
        const int c = (int)(_bx / CELL), r = _h - 1 - (int)(_bz / CELL);
        if (solid(c, r) && _cells[r][c].kind == K_GOAL) { ++_goals; respawn(); }
    }
}

// Behind and above the ball, turning slowly to the way it's rolling (not
// while it's nearly still, or falling, when it holds its heading), and
// looking a little ahead of it. While the ball falls the camera stays put,
// watching it go.
void RollFluxGame::updateCamera(bool snap) {
    if (_falling && !snap) return;
    const float speed = sqrtf(_vx * _vx + _vz * _vz);
    if (speed > YAW_FOLLOW_SPEED) {
        float diff = atan2f(_vx, _vz) - _yaw;
        while (diff > (float)PI) diff -= 2.0f * (float)PI;
        while (diff < -(float)PI) diff += 2.0f * (float)PI;
        _yaw += diff * (snap ? 1.0f : fminf(1.0f, CAMERA_YAW_EASE * _dt));
    }
    const CameraPreset &p = CAMERA_PRESETS[_preset];
    const float fx = sinf(_yaw), fz = cosf(_yaw);
    const float tx = _bx - fx * p.back, ty = _by + p.up, tz = _bz - fz * p.back;
    const float k = snap ? 1.0f : fminf(1.0f, CAMERA_POS_EASE * _dt);
    _camX += (tx - _camX) * k;
    _camY += (ty - _camY) * k;
    _camZ += (tz - _camZ) * k;
    const float lx = _bx + fx * CAMERA_LOOK_AHEAD - _camX;
    const float ly = _by - _camY;
    const float lz = _bz + fz * CAMERA_LOOK_AHEAD - _camZ;
    _camera.setPosition((int32_t)lroundf(_camX), (int32_t)lroundf(_camY), (int32_t)lroundf(_camZ));
    _camera.rotation.x = -atan2f(ly, sqrtf(lx * lx + lz * lz));
    _camera.rotation.y = atan2f(lx, lz);
    _camera.rotation.z = 0;
}

bool RollFluxGame::update(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    (void)audio;
    ensureSceneReady(canvas);
    updateFrameScale();

    // Stage 0's controls: A cycles the camera height, B swaps the floor
    // renderer.
    if (input.btnA && !_prevA) { _preset = (_preset + 1) % 3; updateCamera(true); }
    if (input.btnB && !_prevB) _direct = !_direct;
    _prevA = input.btnA;
    _prevB = input.btnB;

    stepBall(input);
    updateCamera(false);

    const unsigned long t0 = micros();
    renderFrame(canvas);
    _renderUs = micros() - t0;
    _renderUsSum += _renderUs;
    ++_renderFrames;
    if (millis() - _renderAvgAt >= 1000) {
        _renderAvgAt = millis();
        _renderAvgUs = _renderUsSum / (_renderFrames ? _renderFrames : 1);
        _renderUsSum = 0;
        _renderFrames = 0;
    }
    drawOverlay(canvas);
    return true;
}

}  // namespace rollflux
