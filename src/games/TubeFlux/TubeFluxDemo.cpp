#include "TubeFluxGame.h"

// The attract demo, and the autopilot that plays it. The autopilot is also
// the host harness's bot, so the demo's player is the one that's tested.

namespace tubeflux {

// Is `lane` unsafe within `lookahead` of the ship? Blocks, crystals (unless
// armed and far enough off to shoot instead), a lane the drone has warned,
// or a drone bolt still coming down it.
bool TubeFluxGame::laneBlocked(int lane, float lookahead) const {
    if (_warnLanes & (1u << lane)) return true;
    for (const auto &b : _bolts) {
        if (b.active && !b.resolved && laneAt(b.angle) == lane) return true;
    }
    auto covers = [lane](const Obstacle &o) {
        for (int k = 0; k < o.lanes; ++k) if ((o.lane + k) % TUBE_SIDES == lane) return true;
        return false;
    };
    for (const auto &o : _obstacles) {
        if (!o.active || o.resolved) continue;
        float z = o.at - _dist;
        if (z < SHIP_Z - BLOCK_DEPTH || z > SHIP_Z + lookahead) continue;
        if (covers(o)) return true;
    }
    const float reach = armed() ? 900.0f : lookahead;
    for (const auto &c : _crystals) {
        if (!c.active || c.resolved) continue;
        float z = c.at - _dist;
        if (z < SHIP_Z - CRYSTAL_WIDTH || z > SHIP_Z + reach) continue;
        if (covers(c)) return true;
    }
    return false;
}

// Steers for the nearest lane that's clear for a while (settling for the
// one clear the longest if none is), goes for pickups, lines up on the
// drone when it's ahead, and fires at crystals in its lane. Sets joyY and
// btnA only: A alternates while it wants to fire, so each on is a press,
// except with rapid fire, where it's held. replanMs > 0 makes it rethink
// its lane only that often, like a person's reaction time.
InputState TubeFluxGame::pilot(float lookahead, bool shootDrone, unsigned long replanMs) {
    InputState in{};
    const int here = laneAt(_angle);

    if (replanMs == 0 || reached(_pilotReplanAt)) {
        _pilotReplanAt = millis() + replanMs;
        int target = here;
        for (float w = lookahead; w >= 300.0f && laneBlocked(target, w); w *= 0.5f) {
            for (int step = 1; step <= TUBE_SIDES / 2; ++step) {
                int r = (here + step) % TUBE_SIDES, l = (here - step + TUBE_SIDES) % TUBE_SIDES;
                if (!laneBlocked(r, w)) { target = r; break; }
                if (!laneBlocked(l, w)) { target = l; break; }
            }
        }
        if (_pickupActive && _pickupAt - _dist < 3000.0f && !laneBlocked(_pickupLane, 900.0f)) {
            target = _pickupLane;
        }
        if (shootDrone && _chase == CHASE_AHEAD && armed()) {
            int dl = laneAt(_droneAngle);
            if (!laneBlocked(dl, 900.0f)) target = dl;
        }
        _pilotTarget = target;
    }

    float err = deltaDeg((float)_pilotTarget * LANE_DEG, _angle);
    in.joyY = STEER_SIGN * constrain(err / 10.0f, -1.0f, 1.0f);

    bool fire = false;
    if (armed()) {
        if (shootDrone && _chase == CHASE_AHEAD && fabsf(deltaDeg(_angle, _droneAngle)) < 15.0f) fire = true;
        for (const auto &c : _crystals) {
            float z = c.at - _dist;
            if (c.active && c.lane == here && z > SHIP_Z && z < 3500.0f) { fire = true; break; }
        }
    }
    const bool rapid = _gunLevel >= GUN_MAX_LEVEL;
    in.btnA = fire && (rapid || !_pilotPrevA);
    _pilotPrevA = in.btnA;
    return in;
}

// A fresh run, fast-forwarded to a random tier with the gear you'd have by
// then: gun from the crystal tier, twin guns and rapid fire from theirs.
// From the chase tier, sometimes a drone is due almost at once.
void TubeFluxGame::startDemo() {
    resetRun();
    const int tier = (int)random(DEMO_MIN_TIER, DEMO_MAX_TIER + 1);
    _dist = (float)(tier - 1) * TIER_DISTANCE + 2000.0f;
    _tier = tierFor(_dist);
    _speed = tierSpeed();
    applyTierPalette();
    _gunLevel = tier >= RAPID_TIER ? 3 : tier >= TWIN_TIER ? 2 : tier >= CRYSTAL_TIER ? 1 : 0;
    // Everything scheduled by distance restarts from here.
    _nextSpawnAt = _dist + SPAWN_AHEAD * 0.6f;
    _safeLaneMovedAt = _dist;
    _nextBendAt = _dist;
    _nextUpgradeAt = _nextShieldAt = _dist;
    if (tier >= CHASE_FIRST_TIER) {
        _nextChaseAt = random(0, 100) < DEMO_CHASE_PCT ? _dist + 4000.0f : _dist + CHASE_EVERY;
    }
    _pilotPrevA = false;
    _pilotReplanAt = 0;
    _demoUntil = millis() + (unsigned long)random((long)DEMO_MIN_MS, (long)DEMO_MAX_MS + 1);
    _attractSlide = SLIDE_DEMO;
    _attractSlideAt = millis();
}

void TubeFluxGame::updateDemo(GFXcanvas16 &canvas, AudioEngine &audio) {
    InputState in = pilot(DEMO_LOOKAHEAD, true, DEMO_REPLAN_MS);
    in.btnAPressed = in.btnA;   // pilot() only ever turns A on for one frame, or holds it for rapid fire
    _silent = true;
    stepRun(in, audio);
    _silent = false;
    renderRun(canvas);
    if (_shield <= 0 || reached(_demoUntil)) endDemo();
}

// Back to the title, leaving nothing of the demo run behind.
void TubeFluxGame::endDemo() {
    for (auto &o : _obstacles) { o.active = false; if (o.obj) o.obj->enabled = false; }
    hideTransients();
    for (auto &p : _particles.pool) p.active = false;
    _tierBannerUntil = _pickupBannerUntil = _nearMissUntil = _hitFlashUntil = 0;
    enterAttract();
}

}  // namespace tubeflux
