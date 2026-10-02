#include "RollFluxGame.h"

namespace rollflux {

// The floor's height under (x, z), or false over the void. Ramps rise
// across their cell from their low edge.
bool RollFluxGame::floorAt(float x, float z, float &y) const {
    const int c = colAt(x), r = rowAt(z);
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

// A rail runs along a railed cell's edge wherever it borders the void.
bool RollFluxGame::railed(int c, int r, int dir) const {
    if (!solid(c, r) || !(_cells[r][c].flags & F_RAIL)) return false;
    static const int DC[4] = { 0, 0, 1, -1 }, DR[4] = { -1, 1, 0, 0 };
    return !solid(c + DC[dir], r + DR[dir]);
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

// The ball: the stick's push, a ramp's slope and a boost pad accelerate
// it, friction slows it (less on ice), charging a dash holds it back, and it moves in steps of at most
// SUBSTEP, across then along, so it can't pass through a step or a rail
// at any speed. A rise of more than STEP_UP within WALL_PROBE of its
// centre is a wall (in the air, more than LAND_LIP: it catches a lip and
// lands on it); a drop of more than STEP_DOWN, or the void, under its
// centre starts a fall. Fallen FALL_DEPTH below the course, _fellOut says
// so (the rules take a life).
void RollFluxGame::stepBall(const InputState &in) {
    const float dt = _dt;
    _bump = 0;
    float ax = 0, az = 0;
    const bool held = (long)(millis() - _holdUntil) < 0;
    if (!held) stickToWorld(in, ax, az);
    const int c = colAt(_bx), r = rowAt(_bz);
    const uint8_t kind = solid(c, r) ? _cells[r][c].kind : (uint8_t)K_VOID;
    float friction = BALL_FRICTION;
    if (_falling) { ax *= 0.3f; az *= 0.3f; }        // a little steering in the air
    else {
        switch (kind) {
            case K_RAMP_N: az -= SLOPE_GRAVITY; break;
            case K_RAMP_S: az += SLOPE_GRAVITY; break;
            case K_RAMP_E: ax -= SLOPE_GRAVITY; break;
            case K_RAMP_W: ax += SLOPE_GRAVITY; break;
            case K_ICE: ax *= ICE_GRIP; az *= ICE_GRIP; friction *= ICE_FRICTION; break;
            default: break;
        }
        if (isBoost(kind)) {
            static const float BX[4] = { 0, 0, 1, -1 }, BZ[4] = { 1, -1, 0, 0 };
            const int d = kind - K_BOOST_N;
            if (_vx * BX[d] + _vz * BZ[d] < BOOST_SPEED) {
                ax += BX[d] * BOOST_ACCEL;
                az += BZ[d] * BOOST_ACCEL;
            }
            if (_extraSpeed < BOOST_SPEED - BALL_MAX_SPEED) {
                _extraSpeed = BOOST_SPEED - BALL_MAX_SPEED;
                _extraFade = EXTRA_SPEED_DECAY;
            }
        }
    }
    _vx += ax * dt;
    _vz += az * dt;
    if (!_falling) {
        const float keep = 1.0f - friction * dt;
        _vx *= keep > 0 ? keep : 0;
        _vz *= keep > 0 ? keep : 0;
    }
    _extraSpeed -= _extraFade * dt;
    if (_extraSpeed < 0) _extraSpeed = 0;
    // Charging a dash holds the ball back.
    const float cap = _charging ? BALL_MAX_SPEED * DASH_CHARGE_HOLD : BALL_MAX_SPEED + _extraSpeed;
    const float speed = sqrtf(_vx * _vx + _vz * _vz);
    if (speed > cap) { _vx *= cap / speed; _vz *= cap / speed; }

    const float x0 = _bx, z0 = _bz;
    const float dist = (speed > cap ? cap : speed) * dt;
    int n = (int)ceilf(dist / SUBSTEP);
    if (n < 1) n = 1;
    const float sdt = dt / n;
    for (int i = 0; i < n; ++i) {
        float fy;
        // Across, then along. A rail on the edge ahead (while the ball's
        // low enough to meet it) or a rise too big to roll up turns it.
        const int cc = colAt(_bx), cr = rowAt(_bz);
        const float rise = _falling ? LAND_LIP : STEP_UP;
        const bool railHigh = solid(cc, cr) && _by < _cells[cr][cc].h * HEIGHT_STEP + RAIL_HEIGHT;
        if (_vx != 0) {
            const float nx = _bx + _vx * sdt, px = nx + (_vx > 0 ? WALL_PROBE : -WALL_PROBE);
            const bool rail = railHigh && (_vx > 0 ? px > cellX0(cc) + CELL && railed(cc, cr, D_E)
                                                   : px < cellX0(cc) && railed(cc, cr, D_W));
            if (rail || (floorAt(px, _bz, fy) && fy > _by + rise)) {
                if (fabsf(_vx) > _bump) _bump = fabsf(_vx);
                _vx = -_vx * (rail ? RAIL_BOUNCE : WALL_BOUNCE);
            } else _bx = nx;
        }
        if (_vz != 0) {
            const float nz = _bz + _vz * sdt, pz = nz + (_vz > 0 ? WALL_PROBE : -WALL_PROBE);
            const bool rail = railHigh && (_vz > 0 ? pz > cellZ0(cr) + CELL && railed(cc, cr, D_N)
                                                   : pz < cellZ0(cr) && railed(cc, cr, D_S));
            if (rail || (floorAt(_bx, pz, fy) && fy > _by + rise)) {
                if (fabsf(_vz) > _bump) _bump = fabsf(_vz);
                _vz = -_vz * (rail ? RAIL_BOUNCE : WALL_BOUNCE);
            } else _bz = nz;
        }
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
            if (ground && _by <= gy && gy - _by < LAND_LIP) { _by = gy; _vy = 0; _falling = false; }
        }
    }
    rollBall(_bx - x0, _bz - z0);
    _fellOut = _by < -FALL_DEPTH;
}

// A dash: the ball's speed jumps to `strength` times its usual top speed
// along (dx, dz), the extra fading over DASH_FADE_MS.
void RollFluxGame::startDash(float dx, float dz, float strength) {
    const float len = sqrtf(dx * dx + dz * dz);
    if (len < 1e-3f) return;
    const float speed = BALL_MAX_SPEED * strength;
    _vx = dx / len * speed;
    _vz = dz / len * speed;
    _extraSpeed = speed - BALL_MAX_SPEED;
    _extraFade = _extraSpeed * 1000.0f / DASH_FADE_MS;
}

// Turns the ball as it rolls: about the level axis at right angles to the
// way it moved, by the distance over its radius. It keeps turning in the
// air, as a real ball would. The matrix is tidied each time (Gram-Schmidt)
// so rounding doesn't skew it over a long game.
void RollFluxGame::rollBall(float dx, float dz) {
    const float d = sqrtf(dx * dx + dz * dz);
    if (d < 1e-3f) return;
    const float kx = dz / d, kz = -dx / d;            // the axis, (kx, 0, kz)
    const float a = d / BALL_RADIUS, s = sinf(a), c1 = 1.0f - cosf(a);
    // Rodrigues: I + sin(a) K + (1 - cos(a)) K^2, K the axis's cross matrix.
    const float rm[9] = {
        1.0f - c1 * kz * kz, -s * kz,       c1 * kx * kz,
        s * kz,              1.0f - c1,    -s * kx,
        c1 * kx * kz,        s * kx,        1.0f - c1 * kx * kx,
    };
    float out[9];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            out[i * 3 + j] = rm[i * 3] * _rot[j] + rm[i * 3 + 1] * _rot[3 + j] + rm[i * 3 + 2] * _rot[6 + j];
    // Columns are where the ball's own axes point; keep them square and unit.
    float* col[3][3] = { { &out[0], &out[3], &out[6] }, { &out[1], &out[4], &out[7] }, { &out[2], &out[5], &out[8] } };
    for (int k = 0; k < 3; ++k) {
        for (int j = 0; j < k; ++j) {
            const float dp = *col[k][0] * *col[j][0] + *col[k][1] * *col[j][1] + *col[k][2] * *col[j][2];
            for (int e = 0; e < 3; ++e) *col[k][e] -= dp * *col[j][e];
        }
        const float len = sqrtf(*col[k][0] * *col[k][0] + *col[k][1] * *col[k][1] + *col[k][2] * *col[k][2]);
        for (int e = 0; e < 3; ++e) *col[k][e] /= len;
    }
    for (int i = 0; i < 9; ++i) _rot[i] = out[i];
}

}  // namespace rollflux
