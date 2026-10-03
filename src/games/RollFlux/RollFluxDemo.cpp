#include "RollFluxGame.h"

// The attract cycle, and the autopilot that plays its demo. The autopilot
// is also the host harness's bot, so the demo's player is the one that's
// tested on every course.

namespace rollflux {

namespace {
// Corners on each side of a cell (sw 0, se 1, ne 2, nw 3): ours, then the
// neighbour's, along the shared edge in the same order; by Dir.
const int8_t DC[4] = { 0, 0, 1, -1 }, DR[4] = { -1, 1, 0, 0 };
const uint8_t OURS[4][2]   = { { 3, 2 }, { 0, 1 }, { 1, 2 }, { 0, 3 } };
const uint8_t THEIRS[4][2] = { { 0, 1 }, { 3, 2 }, { 0, 3 }, { 1, 2 } };
const int16_t FAR = 32767;
}  // namespace

// --- The attract cycle -----------------------------------------------------------

// Title, how to roll, how to dash, the colours, the high scores, all over a slow orbit
// of course 1; then the demo. A starts a game from any of them.
void RollFluxGame::enterAttract() {
    _phase = PHASE_ATTRACT;
    _slide = SLIDE_TITLE;
    _slideAt = millis();
    _demo = false;
    _silent = false;
    _course = 0;
    _loop = 0;
    loadCourse(0);
}

bool RollFluxGame::updateAttract(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    if (input.btnAPressed) {
        startNewGame(audio);
        return updatePlaying(canvas, InputState{}, audio);
    }
    if (millis() - _slideAt > ATTRACT_SLIDE_MS) {
        _slideAt = millis();
        if (_slide == SLIDE_SCORES) {
            startDemo(audio);
            return updatePlaying(canvas, pilot(true), audio);
        }
        _slide = (AttractSlide)(_slide + 1);
    }
    orbitCamera();
    updateMovers();
    renderFrame(canvas, false);
    switch (_slide) {
        case SLIDE_TITLE:  renderTitle(canvas); break;
        case SLIDE_ROLL:   renderHowTo(canvas, 0); break;
        case SLIDE_DASH:   renderHowTo(canvas, 1); break;
        case SLIDE_PRISM:  renderHowTo(canvas, 2); break;
        default:           renderScores(canvas); break;
    }
    return true;
}

// Round the middle of the course, high, looking down at it.
void RollFluxGame::orbitCamera() {
    _orbit += ORBIT_SPEED * _dt;
    const float cx = _w * CELL * 0.5f, cz = _h * CELL * 0.5f;
    _camX = cx + sinf(_orbit) * ORBIT_RADIUS;
    _camZ = cz - cosf(_orbit) * ORBIT_RADIUS;
    _camY = ORBIT_HEIGHT;
    const float lx = cx - _camX, ly = -_camY, lz = cz - _camZ;
    _camera.setPosition((int32_t)lroundf(_camX), (int32_t)lroundf(_camY), (int32_t)lroundf(_camZ));
    _camera.rotation.x = -atan2f(ly, sqrtf(lx * lx + lz * lz));
    _camera.rotation.y = atan2f(lx, lz);
    _camera.rotation.z = 0;
}

// A real game on a course picked at random, played by the autopilot with
// two dash steps to show off, silently; nothing from it is kept.
void RollFluxGame::startDemo(AudioEngine &audio) {
    _demo = true;
    _silent = true;
    _slide = SLIDE_DEMO;
    _demoUntil = millis() + DEMO_MS;
    _score = 0;
    _lives = START_LIVES;
    _gemsTotal = _goals = _falls = _dashes = 0;
    _crystals = _bumps = _swaps = 0;
    _dashGems = 2 * DASH_GEMS_PER_STEP;
    _course = (int)random(COURSE_COUNT);
    _loop = 0;
    _pilotNextDash = millis() + 3000;
    _pilotChargeUntil = 0;
    startCourse(audio);
}

void RollFluxGame::endDemo() {
    _demo = false;
    _silent = false;
    _charging = false;
    enterAttract();
}

// --- The autopilot -----------------------------------------------------------------

// Can the ball roll from cell (c, r) into its neighbour (nc, nr)? Not up a
// rise too big to roll, and not onto a boost pad against its arrow (it
// throws the ball back); `drop` says it's a step down (a short fall).
bool RollFluxGame::canRoll(int c, int r, int nc, int nr, bool &drop) const {
    drop = false;
    if (!solid(c, r) || !solid(nc, nr)) return false;
    int d = 0;
    while (d < 4 && !(c + DC[d] == nc && r + DR[d] == nr)) ++d;
    if (d == 4) return false;
    const uint8_t k = _cells[nr][nc].kind;
    static const uint8_t AGAINST[4] = { K_BOOST_S, K_BOOST_N, K_BOOST_W, K_BOOST_E };
    if (k == AGAINST[d]) return false;
    float a[4], b[4];
    cornerHeights(c, r, a);
    cornerHeights(nc, nr, b);
    for (int i = 0; i < 2; ++i) {
        const float ours = a[OURS[d][i]], theirs = b[THEIRS[d][i]];
        if (theirs > ours + STEP_UP) return false;
        if (theirs < ours - STEP_DOWN) drop = true;
    }
    return true;
}

// Next to the void with no rail between: somewhere to take carefully.
bool RollFluxGame::exposed(int c, int r) const {
    for (int d = 0; d < 4; ++d)
        if (!solid(c + DC[d], r + DR[d]) && !railed(c, r, d)) return true;
    return false;
}

// The cheapest way from (c, r) to every cell (Dijkstra, the frontier kept
// as a short list to pick the nearest from): a step costs 2, more where
// it's riskier (an exposed cell, ice, a drop). _from[] holds the way back.
void RollFluxGame::planDistances(int c0, int r0) {
    const int n = MAX_COURSE_W * MAX_COURSE_H;
    for (int i = 0; i < n; ++i) { _dist[i] = FAR; _from[i] = -1; }
    if (!solid(c0, r0)) return;
    uint16_t* open = _planOpen;
    bool* done = _planDone;
    for (int i = 0; i < n; ++i) done[i] = false;
    int count = 0;
    const int start = r0 * MAX_COURSE_W + c0;
    _dist[start] = 0;
    open[count++] = (uint16_t)start;
    while (count > 0) {
        int bi = 0;
        for (int k = 1; k < count; ++k) if (_dist[open[k]] < _dist[open[bi]]) bi = k;
        const int best = open[bi];
        open[bi] = open[--count];
        if (done[best]) continue;
        done[best] = true;
        const int c = best % MAX_COURSE_W, r = best / MAX_COURSE_W;
        for (int d = 0; d < 4; ++d) {
            const int nc = c + DC[d], nr = r + DR[d];
            bool drop;
            if (!canRoll(c, r, nc, nr, drop)) continue;
            const int j = nr * MAX_COURSE_W + nc;
            if (done[j]) continue;
            int cost = 2;
            const uint8_t nk = _cells[nr][nc].kind;
            if (exposed(nc, nr)) cost += 3;
            if (nk == K_ICE) cost += 2;
            if (drop) cost += 6;
            if (nk == K_BUMPER) cost += 6;
            if (isConveyor(nk) && nk - K_CONV_N == (d ^ 1)) cost += 3;   // against its run
            if (_dist[best] + cost < _dist[j]) {
                if (_dist[j] == FAR && count < n) open[count++] = (uint16_t)j;
                _dist[j] = (int16_t)(_dist[best] + cost);
                _from[j] = (int16_t)best;
            }
        }
        // A moving part links its two landings (both ways): the wait and the
        // ride cost more than the cells between.
        const CourseDef &def = courseDef();
        for (int k = 0; k < _moverCount; ++k) {
            const MoverDef &m = def.movers[k];
            if (m.lac < 0) continue;
            int j = -1;
            if (c == m.lac && r == m.lar) j = m.lbr * MAX_COURSE_W + m.lbc;
            else if (c == m.lbc && r == m.lbr) j = m.lar * MAX_COURSE_W + m.lac;
            if (j < 0 || done[j]) continue;
            const int cost = 12 + 4 * (abs(m.lac - m.lbc) + abs(m.lar - m.lbr));
            if (_dist[best] + cost < _dist[j]) {
                if (_dist[j] == FAR && count < n) open[count++] = (uint16_t)j;
                _dist[j] = (int16_t)(_dist[best] + cost);
                _from[j] = (int16_t)best;
            }
        }
    }
}

// The way from the ball's cell to (tc, tr), as a list of cells.
void RollFluxGame::planTo(int tc, int tr) {
    _pathLen = _pathPos = 0;
    planDistances(colAt(_bx), rowAt(_bz));
    int i = tr * MAX_COURSE_W + tc;
    if (_dist[i] == FAR) return;
    int n = 0;
    for (int k = i; k >= 0 && n < MAX_PATH; k = _from[k]) _path[n++] = (uint16_t)k;
    for (int a = 0, b = n - 1; a < b; ++a, --b) { const uint16_t t = _path[a]; _path[a] = _path[b]; _path[b] = t; }
    _pathLen = n;
    _targetC = tc;
    _targetR = tr;
}

// Where to go: the nearest goal cell, unless a gem is close (by the way
// there, not as the crow flies), when that first.
void RollFluxGame::pickTarget() {
    planDistances(colAt(_bx), rowAt(_bz));
    int gc = -1, gr = -1, gem = -1, gemC = 0, gemR = 0;
    for (int r = 0; r < _h; ++r)
        for (int c = 0; c < _w; ++c) {
            const int d = planDist(c, r);
            if (d == FAR) continue;
            const Cell &k = _cells[r][c];
            if (k.kind == K_GOAL && (gc < 0 || d < planDist(gc, gr))) { gc = c; gr = r; }
            if ((k.flags & (F_GEM | F_TAKEN)) == F_GEM && d <= 16 && (gem < 0 || d < gem)) {
                gem = d; gemC = c; gemR = r;
            }
        }
    if (gem >= 0) planTo(gemC, gemR);
    else if (gc >= 0) planTo(gc, gr);
    else _pathLen = 0;
}

// The stick (and A) that roll the ball along the planned way: aim for the
// furthest cell ahead it can roll straight to without leaving the way, at
// a pace that drops for turns, ice and edges, steering out the difference
// between the velocity it wants and the one it has. Dashes on a long safe
// straight when it has a step, now and then.
InputState RollFluxGame::pilot(bool useDash) {
    InputState in{};
    if ((long)(millis() - _holdUntil) < 0 || _phase != PHASE_PLAYING) { _pathLen = 0; _pilotLink = -1; return in; }
    if (_falling) return in;
    if (_pilotLink >= 0) return linkPilot();
    const int bc = colAt(_bx), br = rowAt(_bz);
    const bool targetGone = _targetC >= 0 && (_cells[_targetR][_targetC].flags & F_TAKEN);
    const bool forGoal = _targetC >= 0 && _cells[_targetR][_targetC].kind == K_GOAL;
    if (_pathLen == 0 || targetGone || (forGoal && (long)(millis() - _pilotRepickAt) >= 0)) {
        pickTarget();
        _pilotRepickAt = millis() + 1000;
    }
    // Where the ball is on the way; off it, plan again.
    int pos = -1;
    for (int k = _pathPos - 2; k <= _pathPos + 4; ++k)
        if (k >= 0 && k < _pathLen && _path[k] == br * MAX_COURSE_W + bc) { pos = k; break; }
    if (pos < 0) {
        pickTarget();
        for (int k = 0; k < _pathLen && pos < 0; ++k)
            if (_path[k] == br * MAX_COURSE_W + bc) pos = k;
        if (pos < 0) return in;
    }
    _pathPos = pos;
    if (pos >= _pathLen - 1 && _targetC >= 0 && _cells[_targetR][_targetC].kind != K_GOAL) pickTarget();
    if (_pathLen == 0) return in;
    // The next step a moving part's ride (its landings aren't neighbours)?
    if (_pathPos + 1 < _pathLen) {
        const int a = _path[_pathPos], b = _path[_pathPos + 1];
        const int ac = a % MAX_COURSE_W, ar = a / MAX_COURSE_W, bcc = b % MAX_COURSE_W, brr = b / MAX_COURSE_W;
        if (abs(ac - bcc) + abs(ar - brr) != 1) {
            const CourseDef &def = courseDef();
            for (int k = 0; k < _moverCount; ++k) {
                const MoverDef &m = def.movers[k];
                const bool fromA = m.lac == ac && m.lar == ar && m.lbc == bcc && m.lbr == brr;
                const bool fromB = m.lbc == ac && m.lbr == ar && m.lac == bcc && m.lar == brr;
                if (!fromA && !fromB) continue;
                _pilotLink = k;
                _pilotLinkFrom = a;
                _pilotLinkTo = b;
                _pilotLinkFromA = fromA;
                _pilotLinkPhase = 0;
                return linkPilot();
            }
        }
    }

    // Aim as far along the way (up to three cells) as a straight line from
    // the ball stays on cells of the way, crossing only edges it can roll
    // over: no cutting a corner over the void, or into a ramp's side.
    auto onWay = [&](float x, float z, int last, int &prev) {
        const int c = colAt(x), r = rowAt(z), i = r * MAX_COURSE_W + c;
        if (!solid(c, r)) return false;
        bool in = false;
        for (int k = _pathPos; k <= last && !in; ++k) in = _path[k] == i;
        if (!in) return false;
        if (i != prev) {
            const int pc = prev % MAX_COURSE_W, pr = prev / MAX_COURSE_W;
            bool drop;
            if (abs(pc - c) + abs(pr - r) != 1 || !canRoll(pc, pr, c, r, drop)) return false;
            prev = i;
        }
        return true;
    };
    int aim = _pathPos + 1 < _pathLen ? _pathPos + 1 : _pathLen - 1;
    for (int k = _pathPos + 3 < _pathLen ? _pathPos + 3 : _pathLen - 1; k > aim; --k) {
        const float ex = cellX0(_path[k] % MAX_COURSE_W) + CELL * 0.5f;
        const float ez = cellZ0(_path[k] / MAX_COURSE_W) + CELL * 0.5f;
        bool clear = true;
        // Down the middle and either side, the ball's width apart.
        const float lx = ez - _bz, lz = -(ex - _bx), ll = sqrtf(lx * lx + lz * lz) + 1e-3f;
        const float ox = lx / ll * BALL_RADIUS * 0.8f, oz = lz / ll * BALL_RADIUS * 0.8f;
        for (int side = -1; side <= 1 && clear; ++side) {
            const float sx = _bx + ox * side, sz = _bz + oz * side;
            int prev = rowAt(sz) * MAX_COURSE_W + colAt(sx);
            if (!solid(colAt(sx), rowAt(sz))) { clear = false; break; }
            for (int sIdx = 1; sIdx <= 16 && clear; ++sIdx) {
                const float t = sIdx / 16.0f;
                clear = onWay(sx + (ex - _bx) * t, sz + (ez - _bz) * t, k, prev);
            }
        }
        if (clear) { aim = k; break; }
    }
    const float tx = cellX0(_path[aim] % MAX_COURSE_W) + CELL * 0.5f;
    const float tz = cellZ0(_path[aim] / MAX_COURSE_W) + CELL * 0.5f;
    // The pace: easy through turns, on ice and along exposed edges.
    float want = 520.0f;
    int straight = 0;
    int dir = -1;
    for (int k = _pathPos; k + 1 < _pathLen && k < _pathPos + 7; ++k) {
        const int d = _path[k + 1] - _path[k];
        if (dir >= 0 && d != dir) { if (k < _pathPos + 3) want = fminf(want, 260.0f); break; }
        dir = d;
        ++straight;
    }
    for (int k = _pathPos; k < _pathLen && k < _pathPos + 3; ++k) {
        const int c = _path[k] % MAX_COURSE_W, r = _path[k] / MAX_COURSE_W;
        if (_cells[r][c].kind == K_ICE) want = fminf(want, 240.0f);
        if (exposed(c, r)) want = fminf(want, 340.0f);
    }
    float dx = tx - _bx, dz = tz - _bz;
    const float d = sqrtf(dx * dx + dz * dz) + 1e-3f;
    if (aim == _pathLen - 1) want = fminf(want, 120.0f + d * 2.0f);
    // The push it wants: closing the gap to the velocity it wants, plus
    // what friction takes and what a ramp's slope pulls back; as a share
    // of what the stick can give here (less on ice).
    const uint8_t here = _cells[br][bc].kind;
    const bool ice = here == K_ICE;
    const float grip = ice ? ICE_GRIP : 1.0f, friction = BALL_FRICTION * (ice ? ICE_FRICTION : 1.0f);
    float ax = (dx / d * want - _vx) * 5.0f + friction * _vx;
    float az = (dz / d * want - _vz) * 5.0f + friction * _vz;
    if (here == K_RAMP_N) az += SLOPE_GRAVITY;
    if (here == K_RAMP_S) az -= SLOPE_GRAVITY;
    if (here == K_RAMP_E) ax += SLOPE_GRAVITY;
    if (here == K_RAMP_W) ax -= SLOPE_GRAVITY;
    if (isConveyor(here)) {
        static const float CX[4] = { 0, 0, 1, -1 }, CZ[4] = { 1, -1, 0, 0 };
        const int cd = here - K_CONV_N;
        if (_vx * CX[cd] + _vz * CZ[cd] < CONVEYOR_SPEED) { ax -= CX[cd] * CONVEYOR_ACCEL; az -= CZ[cd] * CONVEYOR_ACCEL; }
    }
    ax /= BALL_ACCEL * grip;
    az /= BALL_ACCEL * grip;
    const float m = sqrtf(ax * ax + az * az);
    if (m > 1.0f) { ax /= m; az /= m; }
    // Into the camera-relative stick (landscape: screen up is -joyX, right +joyY).
    const float fx = sinf(_yaw), fz = cosf(_yaw), rx = cosf(_yaw), rz = -sinf(_yaw);
    in.joyX = -(ax * fx + az * fz);
    in.joyY = ax * rx + az * rz;

    // Colour: the first cell ahead that needs one (a gate, a bridge). If
    // it's not the ball's, swap: straight away if nothing between needs
    // the ball's colour as it is; on a bridge of its own colour (or in a
    // gate), only at the edge into the next cell. A swap is a tap of B,
    // the stick still for that frame.
    if (_pilotB) {
        _pilotB = false;                       // let go: that's the tap
    } else if ((long)(millis() - _swapReadyAt) >= 0 && !_charging) {
        const int ownNeed = needsColour(bc, br);
        for (int k = _pathPos + 1; k < _pathLen && k <= _pathPos + 4; ++k) {
            const int nc = _path[k] % MAX_COURSE_W, nr = _path[k] / MAX_COURSE_W;
            const int need = needsColour(nc, nr);
            if (need < 0) continue;
            if (need != _polarity) {
                bool now = ownNeed < 0 || ownNeed == need;
                if (!now && k == _pathPos + 1) {
                    // At the edge into it: within reach of the boundary.
                    const float ex = cellX0(nc) + CELL * 0.5f - _bx, ez = cellZ0(nr) + CELL * 0.5f - _bz;
                    const float toEdge = fmaxf(fabsf(ex), fabsf(ez)) - CELL * 0.5f;
                    now = toEdge < 20.0f + sqrtf(_vx * _vx + _vz * _vz) * 0.04f;
                }
                if (now) {
                    _pilotB = true;
                    in.btnB = true;
                    in.joyX = in.joyY = 0;
                    return in;
                }
            }
            break;
        }
    }

    // A dash: hold A a full charge, then let go.
    if (_charging || (long)(millis() - _pilotChargeUntil) < 0) {
        in.btnA = (long)(millis() - _pilotChargeUntil) < 0;
    } else if (useDash && dashSteps() > 0 && straight >= 6 && (long)(millis() - _pilotNextDash) >= 0) {
        bool safe = true;
        for (int k = _pathPos; k < _pathLen && k < _pathPos + 7; ++k) {
            const int c = _path[k] % MAX_COURSE_W, r = _path[k] / MAX_COURSE_W;
            if (_cells[r][c].kind == K_ICE || exposed(c, r) || isRamp(_cells[r][c].kind)) safe = false;
        }
        if (safe && sqrtf(_vx * _vx + _vz * _vz) > 250.0f) {
            _pilotChargeUntil = millis() + DASH_CHARGE_MS + 20;
            _pilotNextDash = millis() + 5000;
            in.btnA = true;
        }
    }
    in.btnAPressed = in.btnA && !_prevA;
    return in;
}

// The stick that closes the gap to the velocity wanted (with friction's
// drag allowed for), camera-relative.
InputState RollFluxGame::stickFor(float wantVx, float wantVz) const {
    InputState in{};
    float ax = (wantVx - _vx) * 5.0f + BALL_FRICTION * _vx;
    float az = (wantVz - _vz) * 5.0f + BALL_FRICTION * _vz;
    ax /= BALL_ACCEL;
    az /= BALL_ACCEL;
    const float m = sqrtf(ax * ax + az * az);
    if (m > 1.0f) { ax /= m; az /= m; }
    const float fx = sinf(_yaw), fz = cosf(_yaw), rx = cosf(_yaw), rz = -sinf(_yaw);
    in.joyX = -(ax * fx + az * fz);
    in.joyY = ax * rx + az * rz;
    return in;
}

// Crossing by a moving part, from one landing to the other. Wait on the
// landing, at its edge, till the part's there and staying (a bridge lined
// up with time to cross it); a bridge, roll straight over; a slider or
// lift, roll on to its middle, keep still on it till it waits at the far
// end, and roll off. Back to the plan on the far landing.
InputState RollFluxGame::linkPilot() {
    const MoverDef &m = courseDef().movers[_pilotLink];
    const Mover &mv = _movers[_pilotLink];
    const int fc = _pilotLinkFrom % MAX_COURSE_W, fr = _pilotLinkFrom / MAX_COURSE_W;
    const int tc = _pilotLinkTo % MAX_COURSE_W, tr = _pilotLinkTo / MAX_COURSE_W;
    const float fx = cellX0(fc) + CELL * 0.5f, fz = cellZ0(fr) + CELL * 0.5f;
    const float tx = cellX0(tc) + CELL * 0.5f, tz = cellZ0(tr) + CELL * 0.5f;
    const bool bridge = m.type == M_BRIDGE;
    const bool onFrom = colAt(_bx) == fc && rowAt(_bz) == fr;
    const bool onTo = colAt(_bx) == tc && rowAt(_bz) == tr && _onMover < 0;
    auto toward = [&](float x, float z, float speed) {
        const float dx = x - _bx, dz = z - _bz, d = sqrtf(dx * dx + dz * dz) + 1e-3f;
        const float v = fminf(speed, d * 3.0f);
        return stickFor(dx / d * v, dz / d * v);
    };
    if (onTo && _pilotLinkPhase > 0) { _pilotLink = -1; _pathLen = 0; return InputState{}; }
    const bool startB = !_pilotLinkFromA;      // the end the part must be at to board
    switch (_pilotLinkPhase) {
        case 0: {   // wait at the landing's edge
            const float dx = tx - fx, dz = tz - fz, d = sqrtf(dx * dx + dz * dz) + 1e-3f;
            const unsigned long need = bridge ? (unsigned long)(m.len * CELL / 380.0f * 1000.0f) + 900 : 900;
            if (moverReady(_pilotLink, startB, need)) _pilotLinkPhase = 1;
            return toward(fx + dx / d * CELL * 0.22f, fz + dz / d * CELL * 0.22f, 150.0f);
        }
        case 1:     // on to it
            if (bridge) return toward(tx, tz, 380.0f);
            if (onFrom && !moverReady(_pilotLink, startB, 300)) { _pilotLinkPhase = 0; return InputState{}; }
            if (_onMover == _pilotLink && fabsf(_bx - mv.x) < 45.0f && fabsf(_bz - mv.z) < 45.0f) _pilotLinkPhase = 2;
            return toward(mv.x, mv.z, 260.0f);
        case 2:     // ride it, still on it, till it waits at the far end
            if (moverReady(_pilotLink, !startB, 500)) _pilotLinkPhase = 3;
            return toward(mv.x, mv.z, 120.0f);
        default:    // off on to the far landing
            return toward(tx, tz, 260.0f);
    }
}

}  // namespace rollflux
