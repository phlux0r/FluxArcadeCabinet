#include "RollFluxGame.h"

// The guardians, one at the end of each world: three hits with a dash on
// a lit weak point beats one (plain bumps do nothing). The Sweeper's bar
// spins with weak points at its ends and middle; the Piston slams floor
// up under half its arena at a time, then drops its core to the floor;
// the Prism's core is shielded by panels that swap colours; the Gyre tilts
// its arena, opens a ring of floor a quarter at a time, and has weak
// points riding round the rim. All run on the clock from the course's
// start, like the moving parts.

namespace rollflux {

namespace {
unsigned long sinceStart(unsigned long courseAt) { return millis() - courseAt; }
}  // namespace

// Where it is (its core, pivot or ring's middle), its health, its gems.
void RollFluxGame::guardianSetup() {
    _gHp = guardianCourse() ? GUARDIAN_HP : 0;
    _gLit = -1;
    _gNodes = 0;
    _gHitUntil = 0;
    _gCycle = -1;
    _gSlammed = false;
    _regrowCount = 0;
    _gyreTiltX = _gyreTiltZ = 0;
    if (!guardianCourse()) return;
    int n = 0, sc = 0, sr = 0;
    for (int r = 0; r < _h; ++r)
        for (int c = 0; c < _w; ++c) {
            const uint8_t k = _cells[r][c].kind;
            if (k == K_CORE || k == K_RING) { sc += c; sr += r; ++n; }
        }
    if (n) { _gCentreC = (sc + n / 2) / n; _gCentreR = (sr + n / 2) / n; }
    else if (_moverCount) { _gCentreC = (int)courseDef().movers[0].ac; _gCentreR = (int)courseDef().movers[0].ar; }
    if (courseDef().guardian == 1 || courseDef().guardian == 4) _gNodes = 3;
    else _gNodes = 1;
    for (int i = 0; i < 3; ++i) _gNodeVX[i] = _gNodeVZ[i] = 0;
}

// The Piston's half of the arena slamming this cycle: north of the core
// (and its row) on even cycles, south on odd.
float RollFluxGame::extraLift(int c, int r) const {
    if (!solid(c, r)) return 0;
    const uint8_t k = _cells[r][c].kind;
    if (k != K_PISTON && k != K_CORE) return 0;
    const int g = courseDef().guardian;
    if (k == K_CORE) {
        if (_gHp <= 0) return 0;                      // beaten: it's sunk
        if (g == 3) return PRISM_CORE_RISE;
        const unsigned long t = sinceStart(_courseAt);
        if (t < GUARDIAN_GRACE_MS) return CORE_RISE;
        const unsigned long p = (t - GUARDIAN_GRACE_MS) % PISTON_CYCLE_MS;
        // Down (lit) from the pistons' fall to the next cycle's warning's end.
        if (p >= PISTON_FALL_MS || p < 300) return 0;
        if (p < PISTON_FALL_MS && p >= PISTON_FALL_MS - 300)
            return CORE_RISE * (float)(PISTON_FALL_MS - p) / 300.0f;
        return CORE_RISE;
    }
    if (_gHp <= 0) return 0;
    const unsigned long t = sinceStart(_courseAt);
    if (t < GUARDIAN_GRACE_MS) return 0;
    const unsigned long cycle = (t - GUARDIAN_GRACE_MS) / PISTON_CYCLE_MS;
    const bool north = (cycle & 1) == 0;
    if (north ? r > _gCentreR : r < _gCentreR) return 0;
    const unsigned long p = (t - GUARDIAN_GRACE_MS) % PISTON_CYCLE_MS;
    if (p < PISTON_WARN_MS) return 0;
    if (p < PISTON_WARN_MS + PISTON_RISE_MS) return PISTON_RISE * (float)(p - PISTON_WARN_MS) / PISTON_RISE_MS;
    if (p < PISTON_HOLD_MS) return PISTON_RISE;
    if (p < PISTON_FALL_MS) return PISTON_RISE * (float)(PISTON_FALL_MS - p) / (PISTON_FALL_MS - PISTON_HOLD_MS);
    return 0;
}

// A piston about to slam: its half's warning, this cycle.
bool RollFluxGame::pistonWarning(int c, int r) const {
    if (_gHp <= 0 || !solid(c, r) || _cells[r][c].kind != K_PISTON) return false;
    const unsigned long t = sinceStart(_courseAt);
    if (t < GUARDIAN_GRACE_MS) return false;
    const unsigned long cycle = (t - GUARDIAN_GRACE_MS) / PISTON_CYCLE_MS;
    const bool north = (cycle & 1) == 0;
    if (north ? r > _gCentreR : r < _gCentreR) return false;
    return (t - GUARDIAN_GRACE_MS) % PISTON_CYCLE_MS < PISTON_WARN_MS;
}

// The Gyre's ring: a quarter (by direction from the middle) open at a
// time, turning; the next quarter warns first.
namespace {
int ringQuarter(int c, int r, int cc, int cr) {
    const float a = atan2f((float)(c - cc), (float)(cr - r));     // 0 north, clockwise
    return ((int)floorf((a + (float)PI) / ((float)PI * 0.5f))) & 3;
}
}  // namespace

bool RollFluxGame::ringOpen(int c, int r) const {
    if (_gHp <= 0) return false;
    const unsigned long t = sinceStart(_courseAt);
    if (t < GUARDIAN_GRACE_MS + RING_OPEN_MS) return false;
    return ringQuarter(c, r, _gCentreC, _gCentreR) == (int)((t / RING_OPEN_MS) & 3);
}

bool RollFluxGame::ringWarning(int c, int r) const {
    if (_gHp <= 0) return false;
    const unsigned long t = sinceStart(_courseAt) + RING_WARN_MS;
    if (t < GUARDIAN_GRACE_MS + RING_OPEN_MS) return false;
    return !ringOpen(c, r) && ringQuarter(c, r, _gCentreC, _gCentreR) == (int)((t / RING_OPEN_MS) & 3);
}

// This frame: its weak points (where, which lit), the Prism's panels, the
// Gyre's tilt, the Piston throwing a ball off a rising piston, and gems
// growing back.
void RollFluxGame::updateGuardian() {
    if (!guardianCourse()) return;
    const unsigned long now = millis(), t = sinceStart(_courseAt);
    // Gems grow back.
    for (int i = 0; i < _regrowCount; ) {
        if ((long)(now - _regrow[i].at) >= 0) {
            _cells[_regrow[i].r][_regrow[i].c].flags &= ~F_TAKEN;
            if (_courseGemsTaken > 0) --_courseGemsTaken;
            _regrow[i] = _regrow[--_regrowCount];
        } else ++i;
    }
    if (_gHp <= 0) { _gLit = -1; return; }
    const bool shut = (long)(now - _gHitUntil) < 0 || t < GUARDIAN_GRACE_MS;
    const float cx = cellX0(_gCentreC) + CELL * 0.5f, cz = cellZ0(_gCentreR) + CELL * 0.5f;
    const float floorY = _cells[_gCentreR][_gCentreC].h * HEIGHT_STEP;
    float nx[3], nz[3];
    switch (courseDef().guardian) {
        case 1: {   // the Sweeper: the two arms' ends and the pivot
            for (int i = 0; i < 2 && i < _moverCount; ++i) {
                const Mover &mv = _movers[i];
                const float len = courseDef().movers[i].len * CELL;
                nx[i] = mv.x + sinf(mv.ang) * len;
                nz[i] = mv.z + cosf(mv.ang) * len;
            }
            nx[2] = _movers[0].x; nz[2] = _movers[0].z;
            for (int i = 0; i < 3; ++i) _gNodeY[i] = floorY + SWEEPER_HIGH * 0.5f;
            _gLit = shut ? -1 : (int)((t / SWEEPER_NODE_MS) % 3);
            break;
        }
        case 2: {   // the Piston: its core, lit while it's down
            nx[0] = cx; nz[0] = cz;
            _gNodeY[0] = floorY + extraLift(_gCentreC, _gCentreR) + BALL_RADIUS;
            _gLit = !shut && extraLift(_gCentreC, _gCentreR) < 1.0f ? 0 : -1;
            // A rising piston under the ball throws it up (and a little away from
            // the core: mostly back down inside its rails).
            const unsigned long p = (t - GUARDIAN_GRACE_MS) % PISTON_CYCLE_MS;
            const int cycle = t < GUARDIAN_GRACE_MS ? -1 : (int)((t - GUARDIAN_GRACE_MS) / PISTON_CYCLE_MS);
            if (cycle != _gCycle) { _gCycle = cycle; _gSlammed = false; }
            if (cycle >= 0 && p >= PISTON_WARN_MS && p < PISTON_WARN_MS + PISTON_RISE_MS + 100) {
                if (!_gSlammed) { _gSlammed = true; sfx(SFX_SLAM); }
                const int bc = colAt(_bx), br = rowAt(_bz);
                if (!_falling && solid(bc, br) && _cells[br][bc].kind == K_PISTON && extraLift(bc, br) > 1.0f) {
                    float dx = _bx - cx, dz = _bz - cz;
                    const float d = sqrtf(dx * dx + dz * dz) + 1e-3f;
                    _vx += dx / d * 150.0f;
                    _vz += dz / d * 150.0f;
                    _vy = PISTON_LAUNCH;
                    _by = floorY + extraLift(bc, br) + 1.0f;
                    _falling = true;
                }
            }
            break;
        }
        case 3: {   // the Prism: panels swap colours; its core, always open but after a hit
            const unsigned long period = PRISM_SWAP_MS[GUARDIAN_HP - _gHp];
            const bool flip = (t / period) & 1;
            static const int PDC[4] = { 0, 0, 1, -1 }, PDR[4] = { -1, 1, 0, 0 };
            for (int d = 0; d < 4; ++d) {
                const int c = _gCentreC + PDC[d], r = _gCentreR + PDR[d];
                if (!solid(c, r) || !isGate(_cells[r][c].kind)) continue;
                const bool northSouth = d < 2;
                _cells[r][c].kind = (northSouth != flip) ? K_GATE_C : K_GATE_M;
            }
            nx[0] = cx; nz[0] = cz;
            _gNodeY[0] = floorY + PRISM_CORE_RISE + 50.0f;
            _gLit = shut ? -1 : 0;
            break;
        }
        default: {  // the Gyre: three riding round; the arena's tilt turning
            for (int i = 0; i < 3; ++i) {
                const float a = t / 1000.0f * GYRE_ORBIT_SPEED + i * 2.0f * (float)PI / 3.0f;
                nx[i] = cx + sinf(a) * GYRE_ORBIT;
                nz[i] = cz + cosf(a) * GYRE_ORBIT;
                _gNodeY[i] = floorY + BALL_RADIUS;
            }
            const float ta = t / 1000.0f * GYRE_TURN;
            const bool tilting = t >= GUARDIAN_GRACE_MS;
            _gyreTiltX = tilting ? sinf(ta) * GYRE_TILT : 0;
            _gyreTiltZ = tilting ? cosf(ta) * GYRE_TILT : 0;
            _gLit = shut ? -1 : (int)((t / GYRE_NODE_MS) % 3);
            break;
        }
    }
    for (int i = 0; i < _gNodes; ++i) {
        _gNodeVX[i] = (nx[i] - _gNodeX[i]) / _dt;
        _gNodeVZ[i] = (nz[i] - _gNodeZ[i]) / _dt;
        _gNodeX[i] = nx[i];
        _gNodeZ[i] = nz[i];
    }
}

// After the ball's moved: dashing into the lit weak point is a hit. (The
// Prism's core is hit in the wall test, as the ball meets it.)
void RollFluxGame::guardianHits() {
    if (!guardianCourse() || _gHp <= 0 || _gLit < 0 || courseDef().guardian == 3) return;
    if ((long)(millis() - _dashUntil) >= 0) return;
    const float dx = _bx - _gNodeX[_gLit], dz = _bz - _gNodeZ[_gLit], dy = _by + BALL_RADIUS - _gNodeY[_gLit];
    const float reach = BALL_RADIUS + NODE_RADIUS;
    if (dx * dx + dy * dy + dz * dz < reach * reach) hitGuardian();
}

// A hit: shut and flashing a while, points; the third beats it, and the
// course is clear (with its bonus in the tally).
void RollFluxGame::hitGuardian() {
    if (_gHp <= 0 || (long)(millis() - _gHitUntil) < 0) return;
    --_gHp;
    ++_guardianHits;
    _score += GUARDIAN_HIT_POINTS;
    _gHitUntil = millis() + GUARDIAN_SHUT_MS;
    const int n = _gLit >= 0 ? _gLit : 0;
    // Sparks from the weak point.
    uint32_t seed = (uint32_t)(millis() * 2654435761u);
    auto rnd = [&seed]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0f - 0.5f; };
    for (int i = 0; i < SHARD_COUNT; ++i) {
        Shard &s = _shards[i];
        s.x = _gNodeX[n]; s.z = _gNodeZ[n]; s.y = _gNodeY[n];
        s.vx = rnd() * 1200.0f; s.vz = rnd() * 1200.0f; s.vy = 300.0f + rnd() * 600.0f;
        s.colour = (i & 1) ? 0xFFE0 : 0xF800;
    }
    _shardsUntil = millis() + SHARD_MS;
    _gLit = -1;
    if (_gHp > 0) {
        sfx(SFX_GUARD_HIT);
        banner(_gHp == 1 ? "ONE MORE!" : "HIT!", ArcadeConfig::COLOR_YELLOW, 900);
        return;
    }
    sfx(SFX_GUARD_DOWN);
    _clearGuardian = GUARDIAN_POINTS;
    if (_audio) reachGoal(*_audio);
}

}  // namespace rollflux
