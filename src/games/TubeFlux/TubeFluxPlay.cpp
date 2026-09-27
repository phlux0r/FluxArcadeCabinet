#include "TubeFluxGame.h"

namespace tubeflux {

namespace {

inline float wrapDeg(float a) {
    while (a >= 360.0f) a -= 360.0f;
    while (a <    0.0f) a += 360.0f;
    return a;
}

// Shortest signed difference between two angles round the tunnel.
inline float deltaDeg(float a, float b) {
    float d = a - b;
    while (d >  180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

}  // namespace

int TubeFluxGame::tierFor(float dist) const {
    int t = 1 + (int)(dist / TIER_DISTANCE);
    return t > MAX_TIER ? MAX_TIER : t;
}

float TubeFluxGame::tierSpeed() const {
    float s = BASE_SPEED + SPEED_PER_TIER * (float)(_tier - 1);
    return s > MAX_SPEED ? MAX_SPEED : s;
}

float TubeFluxGame::spawnGap() const {
    float frames = GAP_FRAMES_TIER1 - GAP_FRAMES_PER_TIER * (float)(_tier - 1);
    if (frames < GAP_FRAMES_MIN) frames = GAP_FRAMES_MIN;
    // Distance at the tier's own speed, not the throttled one: boosting
    // brings blocks sooner, which is the point of it.
    float g = frames * tierSpeed();
    // +/- GAP_JITTER, so the rhythm isn't a metronome.
    float j = ((float)random(0, 2001) / 1000.0f - 1.0f) * GAP_JITTER;
    return g * (1.0f + j);
}

void TubeFluxGame::updateSteering(const InputState &input) {
    float target = STEER_SIGN * input.joyY * ROLL_RATE;
    float smooth = ROLL_SMOOTH * _frameScale;
    if (smooth > 1.0f) smooth = 1.0f;
    _rollVel += (target - _rollVel) * smooth;
    _angle = wrapDeg(_angle + _rollVel * _frameScale);
}

void TubeFluxGame::updateSpeed(const InputState &input) {
    float stick = THROTTLE_SIGN * input.joyX;   // +1 = pushed up
    float want = 1.0f;
    if (stick > 0.0f) want = 1.0f + (BOOST_MULT - 1.0f) * stick;
    else              want = 1.0f + (1.0f - BRAKE_MULT) * stick;
    float smooth = THROTTLE_SMOOTH * _frameScale;
    if (smooth > 1.0f) smooth = 1.0f;
    _throttle += (want - _throttle) * smooth;

    _speed = tierSpeed() * _throttle;
    _dist += _speed * _frameScale;
}

// A gate every TIER_DISTANCE: faster, denser, new palette, a chime.
void TubeFluxGame::updateTier(AudioEngine &audio) {
    int t = tierFor(_dist);
    if (t == _tier) return;
    _tier = t;
    _bonus += TIER_POINTS;
    applyTierPalette();
    _tierBannerUntil = millis() + TIER_BANNER_MS;
    // A new curve straight away when bends start, not up to a segment later.
    if (_tier == BEND_START_TIER || _tier == BEND_SHARP_TIER) _nextBendAt = _dist;
    static const int n[] = { 660, 880, 1100, 1320 };
    static const int d[] = {  60,  60,   60,  140 };
    audio.playMelody(n, d, 4);
}

// Picks a new curve every BEND_SEGMENT once bends have started: a random
// direction, straight now and then, gentle until BEND_SHARP_TIER. The
// current bend eases towards it, so curves swing in and out rather than
// snapping.
void TubeFluxGame::updateBend() {
    if (_dist >= _nextBendAt) {
        _nextBendAt = _dist + BEND_SEGMENT;
        float maxBend = _tier >= BEND_SHARP_TIER ? BEND_SHARP
                      : _tier >= BEND_START_TIER ? BEND_GENTLE : 0.0f;
        if (maxBend == 0.0f || random(0, 100) < BEND_STRAIGHT_PCT) {
            _bendTargetX = _bendTargetY = 0.0f;
        } else {
            float dir = radians((float)random(0, 360));
            float mag = maxBend * (0.5f + 0.5f * (float)random(0, 1001) / 1000.0f);
            _bendTargetX = mag * cosf(dir);
            _bendTargetY = mag * sinf(dir);
        }
    }
    float ease = BEND_EASE * _frameScale;
    if (ease > 1.0f) ease = 1.0f;
    _bendX += (_bendTargetX - _bendX) * ease;
    _bendY += (_bendTargetY - _bendY) * ease;
}

// Sideways shift of the tunnel's centre line at depth z ahead of the camera.
void TubeFluxGame::bendOffset(float z, float &x, float &y) const {
    float k = z / BEND_REF_Z;
    k *= k;
    x = _bendX * k;
    y = _bendY * k;
}

// Keeps blocks queued out to SPAWN_AHEAD, one every spawnGap().
void TubeFluxGame::spawnObstacles() {
    while (_nextSpawnAt < _dist + SPAWN_AHEAD) {
        spawnBlock(_nextSpawnAt);
        _nextSpawnAt += spawnGap();
    }
}

void TubeFluxGame::spawnBlock(float at) {
    if (at - _safeLaneMovedAt > SAFE_LANE_SHIFT) {
        _prevSafeLane = _safeLane;
        _safeLane = (_safeLane + TUBE_SIDES + (int)random(-1, 2)) % TUBE_SIDES;
        _safeLaneMovedAt = at;
    }
    // The lanes this block must leave open: the safe lane, plus the one it
    // just moved from while that move's transition lasts. They're adjacent
    // (or the same), so they form one span [open, open + openLanes).
    int open = _safeLane, openLanes = 1;
    if (_prevSafeLane != _safeLane && at - _safeLaneMovedAt < SAFE_LANE_TRANSITION) {
        int step = (_safeLane - _prevSafeLane + TUBE_SIDES) % TUBE_SIDES;   // 1 = moved right
        open = step == 1 ? _prevSafeLane : _safeLane;
        openLanes = 2;
    }

    if (_tier >= CRYSTAL_TIER) {
        int pct = CRYSTAL_PCT + CRYSTAL_PCT_PER_TIER * (_tier - CRYSTAL_TIER);
        if (pct > CRYSTAL_PCT_MAX) pct = CRYSTAL_PCT_MAX;
        if (random(0, 100) < pct && spawnCrystal(at, open, openLanes)) return;
    }

    // Tier 1 teaches with single-lane blocks; wider ones from tier 2.
    int maxLanes = _tier >= 2 ? MAX_BLOCK_LANES : 1;
    int lanes = 1 + (int)random(0, maxLanes);

    Obstacle* slot = nullptr;
    for (auto &o : _obstacles) {
        if (!o.active && o.lanes == lanes) { slot = &o; break; }
    }
    if (!slot) return;   // pool exhausted: skip this one rather than stall

    // A block covers lanes [start, start + lanes). Pick one of the starts
    // that clears the open span, directly rather than by retrying.
    int start = (open + openLanes + (int)random(0, TUBE_SIDES - lanes - openLanes + 1)) % TUBE_SIDES;

    slot->active = true;
    slot->resolved = false;
    slot->lane = start;
    slot->at = at;
    slot->obj->enabled = true;
    placeObstacle(*slot);
}

// Unarmed, a crystal is just another obstacle and keeps off the open span.
// Armed, it's sometimes put in the safe lane itself: that route then has
// to be shot open, which is what the gun is for.
bool TubeFluxGame::spawnCrystal(float at, int open, int openLanes) {
    Obstacle* slot = nullptr;
    for (auto &c : _crystals) {
        if (!c.active) { slot = &c; break; }
    }
    if (!slot) return false;

    int lane;
    if (armed() && random(0, 100) < CRYSTAL_ON_SAFE_PCT) {
        lane = _safeLane;
    } else {
        lane = (open + openLanes + (int)random(0, TUBE_SIDES - 1 - openLanes + 1)) % TUBE_SIDES;
    }
    slot->active = true;
    slot->resolved = false;
    slot->lane = lane;
    slot->at = at;
    slot->obj->enabled = true;
    placeObstacle(*slot);
    return true;
}

void TubeFluxGame::placeObstacle(Obstacle &o) {
    // Blocks follow the bend, so they stay on the walls they're attached to.
    float z = o.at - _dist, bx, by;
    bendOffset(z, bx, by);
    o.obj->setPosition((int32_t)lroundf(bx), (int32_t)lroundf(by), (int32_t)z);
    o.obj->setRotation(0, 0, OBSTACLE_ROLL_SIGN * o.lane * (int32_t)LANE_DEG);
}

void TubeFluxGame::updateObstacles(AudioEngine &audio) {
    for (auto &o : _obstacles) if (o.active) updateObstacle(o, audio);
    for (auto &c : _crystals)  if (c.active) updateObstacle(c, audio);
}

void TubeFluxGame::updateObstacle(Obstacle &o, AudioEngine &audio) {
    const float depth = o.crystal ? (float)CRYSTAL_WIDTH : (float)BLOCK_DEPTH;
    float z = o.at - _dist;   // depth ahead of the camera

    if (z < -depth) {   // gone past the camera
        o.active = false;
        o.obj->enabled = false;
        return;
    }
    placeObstacle(o);
    if (o.resolved) return;

    // Angular gap between the ship and the obstacle's nearest edge.
    float centre = ((float)o.lane + (float)(o.lanes - 1) * 0.5f) * LANE_DEG;
    float edgeGap = fabsf(deltaDeg(_angle, centre)) - (float)o.lanes * LANE_DEG * 0.5f;

    bool overlapping = fabsf(z - SHIP_Z) < (depth + SHIP_DEPTH) * 0.5f;
    if (overlapping && edgeGap < SHIP_HALF_DEG) {
        hitShip(o, audio);
    } else if (z < SHIP_Z - (depth + SHIP_DEPTH) * 0.5f) {
        // Passed it cleanly.
        o.resolved = true;
        if (edgeGap < SHIP_HALF_DEG + NEAR_MISS_DEG) {
            _bonus += NEAR_MISS_POINTS;
            _nearMissUntil = millis() + NEAR_MISS_SHOW_MS;
            audio.playTone(1400, 30);
        }
    }
}

// World position of a point at `radius` from the tunnel's axis, at `angle`
// round it, `z` ahead of the camera: on the bent centre line.
void TubeFluxGame::lanePoint(float angle, float radius, float z, float &x, float &y) const {
    float bx, by;
    bendOffset(z, bx, by);
    float a = radians(angle);
    x = bx + radius * sinf(a);
    y = by - radius * cosf(a);
}

// Pickups: at most one on the tunnel at a time. When the slot is free the
// most important one that's due is placed at the spawn front, in the safe
// lane, so there's always a way to it: the gun if you haven't got it, then
// the next gun upgrade once its tier is reached, then a shield if you're
// missing one. A missed pickup comes round again.
void TubeFluxGame::updatePickup(AudioEngine &audio) {
    if (!_pickupActive && !spawnDuePickup(_dist + SPAWN_AHEAD)) return;

    const float z = _pickupAt - _dist;
    const float laneAngle = (float)_pickupLane * LANE_DEG;
    const float half = (PICKUP_DEPTH + SHIP_DEPTH) * 0.5f;
    if (fabsf(z - SHIP_Z) < half && fabsf(deltaDeg(_angle, laneAngle)) < PICKUP_HALF_DEG) {
        collectPickup(audio);
        return;
    }
    if (z < SHIP_Z - half) {
        missPickup();
        return;
    }

    // Flash (white alternating with the kind's colour) and bob towards the
    // axis and back.
    const bool white = (millis() / 120) & 1;
    if (_pickupKind == PICKUP_SHIELD) {
        _crossMat.color = white ? (uint16_t)0xFFFF : (uint16_t)((4 << 11) | (62 << 5) | 6);    // green
    } else if (_pickupKind == PICKUP_UPGRADE) {
        // Magenta, not cyan: cyan is the shots' colour, and a pickup that
        // looks like your own bolts gets lost among them.
        _pickupMat.color = white ? (uint16_t)0xFFFF : (uint16_t)((31 << 11) | (24 << 5) | 31); // magenta
    } else {
        _pickupMat.color = white ? (uint16_t)0xFFFF : (uint16_t)((31 << 11) | (60 << 5) | 4); // yellow
    }
    float bob = 14.0f * sinf((float)millis() * 0.008f);
    float bx, by;
    bendOffset(z, bx, by);
    float a = radians(laneAngle);
    Renderer::Object* obj = pickupObj();
    obj->setPosition((int32_t)lroundf(bx - bob * sinf(a)), (int32_t)lroundf(by + bob * cosf(a)), (int32_t)z);
    obj->setRotation(0, 0, OBSTACLE_ROLL_SIGN * _pickupLane * (int32_t)LANE_DEG);
}

bool TubeFluxGame::spawnDuePickup(float at) {
    if (!armed()) {
        if (at < _nextGunAt) return false;
        _pickupKind = PICKUP_GUN;
    } else if (_gunLevel < GUN_MAX_LEVEL && at >= _nextUpgradeAt &&
               _tier >= (_gunLevel == 1 ? TWIN_TIER : RAPID_TIER)) {
        _pickupKind = PICKUP_UPGRADE;
    } else if (_shield < SHIELD_MAX && _tier >= SHIELD_PICKUP_TIER && at >= _nextShieldAt) {
        _pickupKind = PICKUP_SHIELD;
    } else {
        return false;
    }
    _pickupActive = true;
    _pickupAt = at;
    _pickupLane = _safeLane;
    pickupObj()->enabled = true;
    return true;
}

void TubeFluxGame::collectPickup(AudioEngine &audio) {
    _pickupActive = false;
    pickupObj()->enabled = false;
    switch (_pickupKind) {
        case PICKUP_GUN:
            _gunLevel = 1;
            _pickupBanner = "PRESS A TO FIRE";
            _pickupBannerColour = ArcadeConfig::COLOR_YELLOW;
            break;
        case PICKUP_UPGRADE:
            ++_gunLevel;
            _pickupBanner = _gunLevel >= GUN_MAX_LEVEL ? "RAPID FIRE: HOLD A" : "TWIN GUNS";
            _pickupBannerColour = ArcadeConfig::COLOR_MAGENTA;
            break;
        case PICKUP_SHIELD:
            if (_shield < SHIELD_MAX) ++_shield;
            ++_shieldsCollected;
            _nextShieldAt = _pickupAt + SHIELD_PICKUP_EVERY;
            _pickupBanner = "SHIELD +1";
            _pickupBannerColour = ArcadeConfig::COLOR_GREEN;
            break;
    }
    _pickupBannerUntil = millis() + PICKUP_BANNER_MS;
    audio.playWAV("/audio/tube_powerup.wav");
}

void TubeFluxGame::missPickup() {
    _pickupActive = false;
    pickupObj()->enabled = false;
    const float next = _dist + SPAWN_AHEAD;
    switch (_pickupKind) {
        case PICKUP_GUN:     _nextGunAt     = next + WEAPON_RETRY;  break;
        case PICKUP_UPGRADE: _nextUpgradeAt = next + UPGRADE_RETRY; break;
        // Half the usual wait: you needed it and it got away.
        case PICKUP_SHIELD:  _nextShieldAt  = next + SHIELD_PICKUP_EVERY * 0.5f; break;
    }
}

void TubeFluxGame::fireShot(float angle) {
    for (auto &s : _shots) {
        if (s.active) continue;
        s.active = true;
        s.at = _dist + SHIP_Z;
        s.angle = angle;
        s.obj->enabled = true;
        return;
    }
}

// Rapid fire (level 3) also fires while A is held; before that it's one
// shot per press. Twin guns (level 2+) put a bolt either side of the lane
// centre, so between them they cover the whole lane.
void TubeFluxGame::tryFire(const InputState &input, AudioEngine &audio) {
    if (!armed()) return;
    const bool rapid = _gunLevel >= GUN_MAX_LEVEL;
    if (!(input.btnAPressed || (rapid && input.btnA))) return;
    if ((long)(millis() - _reloadAt) < 0) return;
    if (_gunLevel >= 2) {
        fireShot(_angle - TWIN_SPREAD_DEG);
        fireShot(_angle + TWIN_SPREAD_DEG);
    } else {
        fireShot(_angle);
    }
    _reloadAt = millis() + (rapid ? RAPID_RELOAD_MS : SHOT_RELOAD_MS);
    audio.playWAV("/audio/shot.wav");
}

// Shots fly down the lane they were fired from and stop at the first thing
// they meet: a crystal shatters, a block just eats the shot. The test is
// swept over the whole step, so a fast shot can't skip through a thin one.
void TubeFluxGame::updateShots(AudioEngine &audio) {
    const float radius = TUBE_RADIUS * cosf(radians(LANE_DEG / 2)) - FLY_HEIGHT;
    for (auto &s : _shots) {
        if (!s.active) continue;
        const float from = s.at;
        s.at += SHOT_SPEED * _frameScale;
        if (s.at - _dist > SHOT_RANGE) {
            s.active = false;
            s.obj->enabled = false;
            continue;
        }

        Obstacle* hit = nullptr;
        auto consider = [&](Obstacle &o) {
            if (!o.active) return;
            const float half = (o.crystal ? (float)CRYSTAL_WIDTH : (float)BLOCK_DEPTH) * 0.5f;
            if (o.at + half < from || o.at - half > s.at) return;
            float centre = ((float)o.lane + (float)(o.lanes - 1) * 0.5f) * LANE_DEG;
            float edgeGap = fabsf(deltaDeg(s.angle, centre)) - (float)o.lanes * LANE_DEG * 0.5f;
            if (edgeGap >= SHOT_HALF_DEG) return;
            if (!hit || o.at < hit->at) hit = &o;
        };
        for (auto &o : _obstacles) consider(o);
        for (auto &c : _crystals) consider(c);

        if (hit) {
            s.active = false;
            s.obj->enabled = false;
            if (hit->crystal) {
                destroyCrystal(*hit, audio);
            } else {
                float x, y;
                lanePoint(s.angle, radius, hit->at - _dist, x, y);
                _particles.emitSparks(Renderer::Vec3f{ x, y, hit->at - _dist - BLOCK_DEPTH * 0.5f },
                                      Renderer::Vec3f{ 0, 0, -1 }, 200.0f, 8);
            }
            continue;
        }
        float x, y;
        lanePoint(s.angle, radius, s.at - _dist, x, y);
        s.obj->setPosition((int32_t)lroundf(x), (int32_t)lroundf(y), (int32_t)(s.at - _dist));
    }
}

void TubeFluxGame::destroyCrystal(Obstacle &o, AudioEngine &audio) {
    o.active = false;
    o.obj->enabled = false;
    ++_crystalsDestroyed;
    _bonus += CRYSTAL_POINTS;
    const float floorR = TUBE_RADIUS * cosf(radians(LANE_DEG / 2));
    float x, y, z = o.at - _dist;
    lanePoint((float)o.lane * LANE_DEG, floorR - CRYSTAL_HEIGHT * 0.5f, z, x, y);
    _particles.emitSparks(Renderer::Vec3f{ x, y, z }, Renderer::Vec3f{ 0, 0, -1 }, 520.0f, 26);
    _particles.emitSparks(Renderer::Vec3f{ x, y, z }, Renderer::Vec3f{ 0, 1, 0 }, 300.0f, 14);
    audio.playWAV("/audio/explosion.wav");
}

void TubeFluxGame::hitShip(Obstacle &o, AudioEngine &audio) {
    o.resolved = true;
    if ((long)(millis() - _invulnUntil) < 0) return;   // still blinking from the last hit

    --_shield;
    _invulnUntil   = millis() + HIT_INVULN_MS;
    _hitFlashUntil = millis() + HIT_FLASH_MS;
    // Sparks where the ship met it: on the floor under the ship, in world space.
    float a = radians(_angle);
    const float r = TUBE_RADIUS * 0.8f;
    _particles.emitSparks(Renderer::Vec3f{ r * sinf(a), -r * cosf(a), SHIP_Z },
                          Renderer::Vec3f{ -sinf(a), cosf(a), 0 }, 320.0f, 18);
    audio.playWAV("/audio/tube_bump.wav");
}

}  // namespace tubeflux
