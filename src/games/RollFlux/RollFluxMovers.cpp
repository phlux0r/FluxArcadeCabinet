#include "RollFluxGame.h"

// The Flux Core's moving parts: sliders, lifts and turning bridges the ball
// rides (solid floor wherever they are, carrying it with them), and
// sweepers that knock it about. All run on the clock from the course's
// start, so where one is is a function of time alone.

namespace rollflux {

namespace {
// 0 to 1 and back, eased at each end.
float smooth(float t) { return t * t * (3.0f - 2.0f * t); }
}  // namespace

// Where mover `m` is `t` ms into the course: waiting at a, moving to b,
// waiting at b, moving back (a sweeper just turns).
void RollFluxGame::moverAt(const MoverDef &m, unsigned long t, float &x, float &z, float &y, float &ang) const {
    const float ax = (m.ac + 0.5f) * CELL, az = (_h - 1 - m.ar + 0.5f) * CELL;
    x = ax; z = az;
    y = m.h * HEIGHT_STEP;
    ang = m.ang0 * (float)PI / 180.0f;
    if (m.type == M_SWEEPER) {
        ang = (m.ang1 + m.ang0 * (t / 1000.0f)) * (float)PI / 180.0f;
        return;
    }
    const unsigned long cycle = 2UL * (m.pauseMs + m.moveMs);
    const unsigned long p = t % cycle;
    float s;
    if (p < m.pauseMs) s = 0;
    else if (p < (unsigned long)m.pauseMs + m.moveMs) s = smooth((float)(p - m.pauseMs) / m.moveMs);
    else if (p < 2UL * m.pauseMs + m.moveMs) s = 1;
    else s = 1.0f - smooth((float)(p - 2UL * m.pauseMs - m.moveMs) / m.moveMs);
    switch (m.type) {
        case M_SLIDER:
            x = ax + ((m.bc + 0.5f) * CELL - ax) * s;
            z = az + ((_h - 1 - m.br + 0.5f) * CELL - az) * s;
            break;
        case M_LIFT:
            y = (m.h + (m.h2 - m.h) * s) * HEIGHT_STEP;
            break;
        default:   // a bridge
            ang = (m.ang0 + (m.ang1 - m.ang0) * s) * (float)PI / 180.0f;
            break;
    }
}

// Waiting at its a end (or b) with at least needMs of the wait left.
bool RollFluxGame::moverReady(int k, bool atB, unsigned long needMs) const {
    const MoverDef &m = courseDef().movers[k];
    const unsigned long cycle = 2UL * (m.pauseMs + m.moveMs);
    const unsigned long p = (millis() - _courseAt) % cycle;
    const unsigned long start = atB ? (unsigned long)m.pauseMs + m.moveMs : 0;
    return p >= start && p + needMs < start + m.pauseMs;
}

// This frame's positions, last frame's kept, and the speeds between.
void RollFluxGame::updateMovers() {
    const CourseDef &def = courseDef();
    for (int k = 0; k < _moverCount; ++k) {
        Mover &mv = _movers[k];
        mv.px = mv.x; mv.pz = mv.z; mv.py = mv.y; mv.pang = mv.ang;
        moverAt(def.movers[k], millis() - _courseAt, mv.x, mv.z, mv.y, mv.ang);
        mv.vx = (mv.x - mv.px) / _dt;
        mv.vz = (mv.z - mv.pz) / _dt;
    }
}

// The top of whatever floor-like part (slider, lift, bridge) is under
// (x, z), the highest if more than one; `which` says whose.
bool RollFluxGame::moverFloor(float x, float z, float &y, int* which) const {
    const CourseDef &def = courseDef();
    bool any = false;
    for (int k = 0; k < _moverCount; ++k) {
        const MoverDef &m = def.movers[k];
        if (m.type == M_SWEEPER) continue;
        const Mover &mv = _movers[k];
        const float dx = x - mv.x, dz = z - mv.z;
        bool in;
        if (m.type == M_BRIDGE) {
            const float along = dx * sinf(mv.ang) + dz * cosf(mv.ang);
            const float across = dx * cosf(mv.ang) - dz * sinf(mv.ang);
            in = fabsf(along) <= m.len * CELL * 0.5f && fabsf(across) <= BRIDGE_HALF_WIDTH;
        } else {
            in = fabsf(dx) <= PLATFORM_HALF && fabsf(dz) <= PLATFORM_HALF;
        }
        if (in && (!any || mv.y > y)) { y = mv.y; any = true; if (which) *which = k; }
    }
    return any;
}

// A ball riding a part goes with it: moved as it moved, turned as a bridge
// turned.
void RollFluxGame::carryBall() {
    if (_onMover < 0 || _falling) return;
    const Mover &mv = _movers[_onMover];
    _bx += mv.x - mv.px;
    _bz += mv.z - mv.pz;
    _by += mv.y - mv.py;
    const float da = mv.ang - mv.pang;
    if (da != 0) {
        // About the pivot, the same way the bridge's own axis turned.
        const float dx = _bx - mv.x, dz = _bz - mv.z, c = cosf(da), s = sinf(da);
        _bx = mv.x + dx * c + dz * s;
        _bz = mv.z - dx * s + dz * c;
    }
}

// After it's moved: on a part's top (not falling, level with it), the
// ball's riding that part.
void RollFluxGame::findOnMover() {
    _onMover = -1;
    if (_falling) return;
    float y;
    int k = -1;
    if (moverFloor(_bx, _bz, y, &k) && fabsf(y - _by) < 3.0f) _onMover = k;
}

// Sweepers' arms: the ball inside one's reach (and low enough) is put back
// out and sent off at the arm's own speed there plus SWEEPER_KICK.
void RollFluxGame::sweepers() {
    const CourseDef &def = courseDef();
    for (int k = 0; k < _moverCount; ++k) {
        const MoverDef &m = def.movers[k];
        if (m.type != M_SWEEPER) continue;
        const Mover &mv = _movers[k];
        if (_by > mv.y + SWEEPER_HIGH || _by < mv.y - BALL_RADIUS) continue;
        const float dirx = sinf(mv.ang), dirz = cosf(mv.ang), len = m.len * CELL;
        const float rx = _bx - mv.x, rz = _bz - mv.z;
        float t = rx * dirx + rz * dirz;
        if (t < 0) t = 0;
        if (t > len) t = len;
        const float qx = mv.x + dirx * t, qz = mv.z + dirz * t;
        const float dx = _bx - qx, dz = _bz - qz, d = sqrtf(dx * dx + dz * dz);
        const float reach = BALL_RADIUS * 0.8f + SWEEPER_HALF_WIDTH;
        if (d >= reach) continue;
        const float nx = d > 1e-3f ? dx / d : dirz, nz = d > 1e-3f ? dz / d : -dirx;
        _bx += nx * (reach - d);
        _bz += nz * (reach - d);
        // The arm's speed where it hit: across it, by how far out.
        const float w = m.ang0 * (float)PI / 180.0f;
        const float ax = w * t * dirz, az = -w * t * dirx;
        float vrx = _vx - ax, vrz = _vz - az;
        const float vn = vrx * nx + vrz * nz;
        if (vn < 0) { vrx -= 1.6f * vn * nx; vrz -= 1.6f * vn * nz; }
        _vx = vrx + ax;
        _vz = vrz + az;
        const float out = _vx * nx + _vz * nz, want = fabsf(ax * nx + az * nz) + SWEEPER_KICK;
        if (out < want) { _vx += nx * (want - out); _vz += nz * (want - out); }
        if (_bump < want) _bump = want;
    }
}

}  // namespace rollflux
