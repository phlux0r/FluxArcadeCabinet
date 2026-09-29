#include "StarFluxGame.h"

// The attract demo, and the autopilot that plays it. The autopilot is also
// the host harness's bot, so the demo's player is the one that's tested.

namespace starflux {

namespace {
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
}  // namespace

// How dangerous it would be to sit at (x, y) over the next second or so:
// enemy shots and rocks by where they'll cross the ship's plane (sooner
// counts more), and fighters passing through it.
float StarFluxGame::threatAt(float x, float y) const {
    float threat = 0;
    for (const auto &e : _eshots) {
        if (!e.active || e.vz >= 0 || e.z < SHIP_Z) continue;
        float t = (e.z - SHIP_Z) / -e.vz;
        if (t > 45.0f) continue;
        float cx = e.x + e.vx * t, cy = e.y + e.vy * t;
        float d2 = (cx - x) * (cx - x) + (cy - y) * (cy - y);
        const float r = SHIP_HIT_R + 35.0f;
        threat += expf(-d2 / (2.0f * r * r)) * (1.0f + (45.0f - t) / 45.0f);
    }
    for (const auto &k : _rocks) {
        if (!k.active || k.z < SHIP_Z - k.r) continue;
        float t = (k.z - SHIP_Z) / FLY_SPEED;
        if (t > 60.0f) continue;
        float cx = k.x + k.vx * t, cy = k.y + k.vy * t;
        float d2 = (cx - x) * (cx - x) + (cy - y) * (cy - y);
        const float r = k.r * 0.8f + SHIP_HIT_R + 30.0f;
        threat += 1.5f * expf(-d2 / (2.0f * r * r * 0.5f)) * (1.0f + (60.0f - t) / 60.0f);
    }
    for (const auto &f : _fighters) {
        if (!f.active || !reached(f.startAt) || fabsf(f.z - SHIP_Z) > 700.0f) continue;
        float d2 = (f.x - x) * (f.x - x) + (f.y - y) * (f.y - y);
        const float r = FIGHTER_R + SHIP_HIT_R + 30.0f;
        threat += expf(-d2 / (2.0f * r * r * 0.5f));
    }
    return threat;
}

// Is something worth shooting on the line from (x, y) straight ahead?
bool StarFluxGame::targetOnLine(float x, float y) const {
    auto on = [&](float tx, float ty, float r) {
        float dx = tx - x, dy = ty - y, rr = r + SHOT_HIT_PAD;
        return dx * dx + dy * dy < rr * rr;
    };
    for (const auto &f : _fighters) {
        if (f.active && reached(f.startAt) && f.z > SHIP_Z + 150.0f && f.z < SHIP_Z + SHOT_RANGE &&
            on(f.x, f.y, FIGHTER_R)) return true;
    }
    for (const auto &k : _rocks) {
        if (k.active && k.z > SHIP_Z + 150.0f && k.z < SHIP_Z + SHOT_RANGE && on(k.x, k.y, k.r)) return true;
    }
    if (_bossActive && _stage == STAGE_RUN) {
        for (int p = 0; p < 3; ++p) {
            if (!bossPartAlive(p)) continue;
            float px, py, pz;
            bossPartPos(p, px, py, pz);
            if (on(px, py, p == 2 ? CORE_R : CANNON_R)) return true;
        }
    }
    return false;
}

// Where to put the ship to hit something: the nearest fighter ahead, led
// by where it'll be when a shot gets there; else a boss weak point; else
// the nearest rock. False if there's nothing to shoot.
bool StarFluxGame::pickAim(float &x, float &y) const {
    float best = 1e9f;
    bool found = false;
    for (const auto &f : _fighters) {
        if (!f.active || !reached(f.startAt) || f.z < SHIP_Z + 300.0f || f.z > SHIP_Z + 5000.0f) continue;
        if (f.z >= best) continue;
        unsigned long t = millis() - f.startAt;
        unsigned long lead = (unsigned long)((f.z - SHIP_Z) / SHOT_SPEED * (float)REFERENCE_FRAME_MS);
        float lx, ly, lz;
        pathPoint(f, t + lead, lx, ly, lz);
        best = f.z;
        x = lx; y = ly;
        found = true;
    }
    if (found) return true;
    if (_bossActive && _stage == STAGE_RUN && millis() - _bossAt > BOSS_ENTER_MS) {
        for (int p = 0; p < 3; ++p) {
            if (!bossPartAlive(p)) continue;
            float px, py, pz;
            bossPartPos(p, px, py, pz);
            // The nearer cannon to where you are, or the core.
            float d = fabsf(px - _shipX);
            if (d < best) { best = d; x = px; y = py; found = true; }
        }
        if (found) return true;
    }
    for (const auto &k : _rocks) {
        if (!k.active || k.z < SHIP_Z + 1200.0f || k.z >= best) continue;
        best = k.z;
        x = k.x; y = k.y;
        found = true;
    }
    return found;
}

// Picks the safest good spot near the aim point from a grid across the
// box and steers for it; fires while something is on its line; drops a
// bomb (useBombs) when a pack of fighters is ahead or it's cornered.
// A is tapped every other frame, so each shot is a press (the fastest
// fire rate); B is pressed for one frame, which is a tap. replanMs > 0
// makes it pick a new spot only that often, like a player's reaction
// time; steering and firing still happen every frame.
InputState StarFluxGame::pilot(bool useBombs, unsigned long replanMs) {
    InputState in{};
    float bestThreat = 0;
    if (replanMs == 0 || reached(_pilotReplanAt)) {
        _pilotReplanAt = millis() + replanMs;
        bestThreat = planPilot();
    } else {
        bestThreat = threatAt(_pilotX, _pilotY);
    }
    in.joyY = STEER_X_SIGN * clampf((_pilotX - _shipX) / 50.0f, -1.0f, 1.0f);
    in.joyX = STEER_Y_SIGN * clampf((_pilotY - _shipY) / 50.0f, -1.0f, 1.0f);

    in.btnA = targetOnLine(_shipX, _shipY) && !_prevA;

    if (_pilotPrevB) {
        _pilotPrevB = false;             // release: that's the tap
    } else if (useBombs && _bombs > 0 && !_bombActive && reached(_pilotBombAt) && _stage == STAGE_RUN) {
        int pack = 0;
        for (const auto &f : _fighters) {
            if (!f.active || !reached(f.startAt)) continue;
            float dx = f.x - _shipX, dy = f.y - _shipY, dz = f.z - BOMB_FUSE_Z;
            if (dx * dx + dy * dy + dz * dz < BOMB_RADIUS * BOMB_RADIUS * 0.6f) ++pack;
        }
        bool bossShot = _bossActive && coreOpen() && random(0, 100) < 2;
        if (pack >= 4 || bestThreat > 1.6f || bossShot) {
            in.btnB = true;
            _pilotPrevB = true;
            _pilotBombAt = millis() + 5000;
        }
    }
    return in;
}

// Where the pilot wants to be (_pilotX, _pilotY): the cheapest of the aim
// point, where it is, a grid across the box and a ring round it, weighing
// danger most, then lining up a shot, then not moving far. Returns the
// danger at the spot it chose.
float StarFluxGame::planPilot() {
    float ax = 0, ay = 40.0f;
    const bool aiming = pickAim(ax, ay);
    // A ring ahead is worth going for when the shield's down some.
    for (const auto &r : _rings) {
        if (r.active && !r.resolved && r.z > SHIP_Z + 300.0f && r.z < 4000.0f && _shield < SHIELD_MAX - 10) {
            ax = r.x; ay = r.y;
        }
    }
    ax = clampf(ax, -BOX_X, BOX_X);
    ay = clampf(ay, BOX_Y_MIN, BOX_Y_MAX);

    float bx = _shipX, by = _shipY, bestCost = 1e9f, bestThreat = 0;
    auto consider = [&](float x, float y) {
        x = clampf(x, -BOX_X, BOX_X);
        y = clampf(y, BOX_Y_MIN, BOX_Y_MAX);
        float th = threatAt(x, y);
        float da = hypotf(x - ax, y - ay), dm = hypotf(x - _shipX, y - _shipY);
        float cost = th * 900.0f + da * (aiming ? 0.6f : 0.15f) + dm * 0.25f;
        if (cost < bestCost) { bestCost = cost; bx = x; by = y; bestThreat = th; }
    };
    consider(ax, ay);
    consider(_shipX, _shipY);
    for (int i = 0; i <= 6; ++i) {
        for (int j = 0; j <= 4; ++j) {
            consider(-BOX_X + (2.0f * BOX_X) * (float)i / 6.0f,
                     BOX_Y_MIN + (BOX_Y_MAX - BOX_Y_MIN) * (float)j / 4.0f);
        }
    }
    for (int k = 0; k < 8; ++k) {
        float a = (float)k * (PI / 4.0f);
        consider(_shipX + cosf(a) * 110.0f, _shipY + sinf(a) * 110.0f);
    }
    _pilotX = bx; _pilotY = by;
    return bestThreat;
}

// A fresh run started part way through the stage (or at the boss now and
// then), with a few seconds' grace.
void StarFluxGame::startDemo() {
    resetRun();
    int seg = (int)random(0, segmentCount());
    _stage = STAGE_RUN;
    _stageAt = millis();
    _shipY = 0;
    _pilotPrevB = false;
    _pilotBombAt = millis() + 3000;
    _pilotReplanAt = 0;
    _invulnUntil = millis() + 1500;
    startSegment(seg);
    _demoUntil = millis() + (unsigned long)random((long)DEMO_MIN_MS, (long)DEMO_MAX_MS + 1);
    _attractSlide = SLIDE_DEMO;
    _attractSlideAt = millis();
}

void StarFluxGame::updateDemo(GFXcanvas16 &canvas, AudioEngine &audio) {
    InputState in = pilot(true, DEMO_REPLAN_MS);
    in.btnAPressed = in.btnA && !_prevA;
    in.btnBPressed = in.btnB;
    _silent = true;
    stepRun(in, audio);
    _silent = false;
    // Wraps up at the end of the stage too: after the boss, the demo's over.
    if (_phase != PHASE_ATTRACT) return;
    renderRun(canvas);
    if (reached(_demoUntil) && _stage != STAGE_BOSS_DEATH) endDemo();
}

// Back to the title, leaving nothing of the demo run behind.
void StarFluxGame::endDemo() {
    clearField();
    _bannerUntil = _hitFlashUntil = 0;
    _stage = STAGE_RUN;
    _shipX = _shipY = _shipVX = _shipVY = _bank = 0;
    _btnBDownAt = 0;
    enterAttract();
}

}  // namespace starflux
