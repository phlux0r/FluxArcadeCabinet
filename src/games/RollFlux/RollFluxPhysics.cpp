#include "RollFluxGame.h"

namespace rollflux {

// The floor's height under (x, z), or false over the void (or a phase
// bridge of the other colour). Ramps rise across their cell from their low
// edge; a crystal wall's top is CRYSTAL_HEIGHT up. A moving part there
// (higher than the cell, if both) counts as floor too.
bool RollFluxGame::floorAt(float x, float z, float &y) const {
    const int c = colAt(x), r = rowAt(z);
    float my;
    const bool mover = _moverCount > 0 && moverFloor(x, z, my);
    if (!standable(c, r)) {
        if (mover) y = my;
        return mover;
    }
    const Cell &cell = _cells[r][c];
    const float fx = (x - cellX0(c)) / CELL, fz = (z - cellZ0(r)) / CELL;
    float base = (float)(cell.h * HEIGHT_STEP);
    switch (cell.kind) {
        case K_RAMP_N: base += HEIGHT_STEP * fz; break;
        case K_RAMP_S: base += HEIGHT_STEP * (1.0f - fz); break;
        case K_RAMP_E: base += HEIGHT_STEP * fx; break;
        case K_RAMP_W: base += HEIGHT_STEP * (1.0f - fx); break;
        case K_CRYSTAL: base += CRYSTAL_HEIGHT; break;
        default: break;
    }
    y = mover && my > base ? my : base;
    return true;
}

// What stops the ball moving its probe to (px, pz): 0 nothing, 1 a wall
// (a rise more than `rise` over `base`), 2 a colour gate of the other
// colour (a cell it isn't already in). A crystal wall met in a dash is
// smashed, and stops nothing.
int RollFluxGame::blockedAt(float px, float pz, int ownC, int ownR, float base, float rise) {
    const int c = colAt(px), r = rowAt(pz);
    if (solid(c, r) && isGate(_cells[r][c].kind) && needsColour(c, r) != _polarity && !(c == ownC && r == ownR))
        return 2;
    float fy;
    if (!floorAt(px, pz, fy) || fy <= base + rise) return 0;
    if (_cells[r][c].kind == K_CRYSTAL && (long)(millis() - _dashUntil) < 0) {
        smashCrystal(c, r);
        return 0;
    }
    return 1;
}

// A crystal wall goes: its cell is floor now, the points scored, shards
// flying from where it stood.
void RollFluxGame::smashCrystal(int c, int r) {
    Cell &k = _cells[r][c];
    k.kind = K_FLOOR;
    ++_crystals;
    _score += CRYSTAL_POINTS;
    const float cx = cellX0(c) + CELL * 0.5f, cz = cellZ0(r) + CELL * 0.5f;
    const float y = k.h * HEIGHT_STEP + CRYSTAL_HEIGHT * 0.5f;
    uint32_t seed = (uint32_t)(c * 977 + r * 131 + millis());
    auto rnd = [&seed]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0f - 0.5f; };
    for (int i = 0; i < SHARD_COUNT; ++i) {
        Shard &s = _shards[i];
        s.x = cx + rnd() * CELL * 0.6f; s.z = cz + rnd() * CELL * 0.6f; s.y = y + rnd() * CRYSTAL_HEIGHT * 0.6f;
        s.vx = rnd() * 900.0f + _vx * 0.3f; s.vz = rnd() * 900.0f + _vz * 0.3f; s.vy = 300.0f + rnd() * 500.0f;
        s.colour = (i & 1) ? 0xF81F : 0x07FF;
    }
    _shardsUntil = millis() + SHARD_MS;
    sfx(SFX_CRYSTAL);
}

// Bumpers near the ball: inside one's reach, the ball's put back out and
// sent off it at BUMPER_KICK at least, the bumper lit.
void RollFluxGame::bumpers() {
    const int c0 = colAt(_bx), r0 = rowAt(_bz);
    for (int r = r0 - 1; r <= r0 + 1; ++r)
        for (int c = c0 - 1; c <= c0 + 1; ++c) {
            if (!solid(c, r) || _cells[r][c].kind != K_BUMPER) continue;
            if (_by > _cells[r][c].h * HEIGHT_STEP + BUMPER_HEIGHT) continue;
            const float dx = _bx - (cellX0(c) + CELL * 0.5f), dz = _bz - (cellZ0(r) + CELL * 0.5f);
            const float reach = BUMPER_RADIUS + BALL_RADIUS * 0.8f;
            const float d = sqrtf(dx * dx + dz * dz);
            if (d >= reach) continue;
            const float nx = d > 1e-3f ? dx / d : 1.0f, nz = d > 1e-3f ? dz / d : 0.0f;
            _bx += nx * (reach - d);
            _bz += nz * (reach - d);
            float vn = _vx * nx + _vz * nz;
            if (vn < 0) { _vx -= 2.0f * vn * nx; _vz -= 2.0f * vn * nz; vn = -vn; }
            if (vn < BUMPER_KICK) { _vx += nx * (BUMPER_KICK - vn); _vz += nz * (BUMPER_KICK - vn); }
            if ((long)(millis() - _bumpUntil) >= 0 || _bumpC != c || _bumpR != r) {
                ++_bumps;
                _score += BUMPER_POINTS;
                sfx(SFX_BUMPER);
            }
            _bumpC = c; _bumpR = r;
            _bumpUntil = millis() + 200;
        }
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
    const uint8_t kind = standable(c, r) ? _cells[r][c].kind : (uint8_t)K_VOID;
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
        if (isConveyor(kind)) {
            static const float CX[4] = { 0, 0, 1, -1 }, CZ[4] = { 1, -1, 0, 0 };
            const int d = kind - K_CONV_N;
            if (_vx * CX[d] + _vz * CZ[d] < CONVEYOR_SPEED) {
                ax += CX[d] * CONVEYOR_ACCEL;
                az += CZ[d] * CONVEYOR_ACCEL;
            }
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
    if ((long)(millis() - _extraHoldUntil) >= 0) _extraSpeed -= _extraFade * dt;
    if (_extraSpeed < 0) _extraSpeed = 0;
    // Charging a dash holds the ball back; after a dash's burst, once it's
    // on the floor, it brakes back to the speed it had before.
    float cap = _charging ? BALL_MAX_SPEED * DASH_CHARGE_HOLD : BALL_MAX_SPEED + _extraSpeed;
    if (!_charging && _dashBrakeDue && (long)(millis() - _dashUntil) >= 0 && !_falling) {
        if (!_braking) { _braking = true; _brakeAt = millis(); }
        const float t = (float)(millis() - _brakeAt) / DASH_BRAKE_MS;
        if (t >= 1.0f) { cap = _dashReturn; _dashBrakeDue = false; _braking = false; }
        else cap = BALL_MAX_SPEED + (_dashReturn - BALL_MAX_SPEED) * t;
    }
    const float speed = sqrtf(_vx * _vx + _vz * _vz);
    if (speed > cap) { _vx *= cap / speed; _vz *= cap / speed; }

    const float x0 = _bx, z0 = _bz;
    const float dist = (speed > cap ? cap : speed) * dt;
    int n = (int)ceilf(dist / SUBSTEP);
    if (n < 1) n = 1;
    const float sdt = dt / n;
    for (int i = 0; i < n; ++i) {
        // Across, then along. A rail on the edge ahead (while the ball's
        // low enough to meet it) or a rise too big to roll up turns it.
        const int cc = colAt(_bx), cr = rowAt(_bz);
        const float rise = _falling ? LAND_LIP : STEP_UP;
        // The rise ahead is measured from the floor where the ball's about
        // to be (rolling), not from where it was: at speed the probe runs a
        // whole substep ahead, and a ramp's rise from the old height would
        // read as a wall. In the air, from the ball itself.
        auto baseAt = [&](float x, float z) {
            float y;
            return !_falling && floorAt(x, z, y) && y > _by ? y : _by;
        };
        const bool railHigh = solid(cc, cr) && _by < _cells[cr][cc].h * HEIGHT_STEP + RAIL_HEIGHT;
        // (A gate of the other colour bounces it too; a crystal wall met
        // in a dash is smashed instead.)
        if (_vx != 0) {
            const float nx = _bx + _vx * sdt, px = nx + (_vx > 0 ? WALL_PROBE : -WALL_PROBE);
            const bool rail = railHigh && (_vx > 0 ? px > cellX0(cc) + CELL && railed(cc, cr, D_E)
                                                   : px < cellX0(cc) && railed(cc, cr, D_W));
            const int hit = rail ? 1 : blockedAt(px, _bz, cc, cr, baseAt(nx, _bz), rise);
            if (hit) {
                if (fabsf(_vx) > _bump) _bump = fabsf(_vx);
                if (hit == 2) sfx(SFX_GATE);
                _vx = -_vx * (rail ? RAIL_BOUNCE : hit == 2 ? GATE_BOUNCE : WALL_BOUNCE);
            } else _bx = nx;
        }
        if (_vz != 0) {
            const float nz = _bz + _vz * sdt, pz = nz + (_vz > 0 ? WALL_PROBE : -WALL_PROBE);
            const bool rail = railHigh && (_vz > 0 ? pz > cellZ0(cr) + CELL && railed(cc, cr, D_N)
                                                   : pz < cellZ0(cr) && railed(cc, cr, D_S));
            const int hit = rail ? 1 : blockedAt(_bx, pz, cc, cr, baseAt(_bx, nz), rise);
            if (hit) {
                if (fabsf(_vz) > _bump) _bump = fabsf(_vz);
                if (hit == 2) sfx(SFX_GATE);
                _vz = -_vz * (rail ? RAIL_BOUNCE : hit == 2 ? GATE_BOUNCE : WALL_BOUNCE);
            } else _bz = nz;
        }
        bumpers();
        if (_moverCount) sweepers();
        // Down: on the floor it follows it; off it (or over a drop), it falls.
        float gy;
        const bool ground = floorAt(_bx, _bz, gy);
        if (!_falling) {
            if (!ground || gy < _by - STEP_DOWN) { _falling = true; _vy = 0; }
            else _by = gy;
        }
        if (_falling) {
            // A dash carries the ball: less gravity during its burst.
            _vy -= GRAVITY * ((long)(millis() - _dashUntil) < 0 ? DASH_GRAVITY : 1.0f) * sdt;
            _by += _vy * sdt;
            // Landing: onto the floor from above, not from under its edge.
            if (ground && _by <= gy && gy - _by < LAND_LIP) { _by = gy; _vy = 0; _falling = false; }
        }
    }
    rollBall(_bx - x0, _bz - z0);
    _fellOut = _by < -FALL_DEPTH;
    // The shards of a smashed crystal fly and fall.
    if ((long)(millis() - _shardsUntil) < 0)
        for (Shard &s : _shards) { s.x += s.vx * dt; s.y += s.vy * dt; s.z += s.vz * dt; s.vy -= GRAVITY * dt; }
}

// A dash: the ball's speed jumps to `strength` times its usual top speed
// along (dx, dz), the extra held for DASH_HOLD_MS and fading over
// DASH_FADE_MS; then it brakes back (stepBall).
void RollFluxGame::startDash(float dx, float dz, float strength) {
    const float len = sqrtf(dx * dx + dz * dz);
    if (len < 1e-3f) return;
    const float speed = BALL_MAX_SPEED * strength;
    _dashReturn = fmaxf(DASH_RETURN_MIN, fminf(sqrtf(_vx * _vx + _vz * _vz), BALL_MAX_SPEED * DASH_CHARGE_HOLD));
    _extraHoldUntil = millis() + DASH_HOLD_MS;
    _dashUntil = _extraHoldUntil + DASH_FADE_MS;
    _dashBrakeDue = true;
    _braking = false;
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
