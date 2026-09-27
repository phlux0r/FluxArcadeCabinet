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
    // Tier 1 teaches with single-lane blocks; wider ones from tier 2.
    int maxLanes = _tier >= 2 ? MAX_BLOCK_LANES : 1;
    int lanes = 1 + (int)random(0, maxLanes);

    Obstacle* slot = nullptr;
    for (auto &o : _obstacles) {
        if (!o.active && o.lanes == lanes) { slot = &o; break; }
    }
    if (!slot) return;   // pool exhausted: skip this one rather than stall

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

void TubeFluxGame::placeObstacle(Obstacle &o) {
    // Blocks follow the bend, so they stay on the walls they're attached to.
    float z = o.at - _dist, bx, by;
    bendOffset(z, bx, by);
    o.obj->setPosition((int32_t)lroundf(bx), (int32_t)lroundf(by), (int32_t)z);
    o.obj->setRotation(0, 0, OBSTACLE_ROLL_SIGN * o.lane * (int32_t)LANE_DEG);
}

void TubeFluxGame::updateObstacles(AudioEngine &audio) {
    for (auto &o : _obstacles) {
        if (!o.active) continue;
        float z = o.at - _dist;   // depth ahead of the camera

        if (z < -(float)BLOCK_DEPTH) {   // gone past the camera
            o.active = false;
            o.obj->enabled = false;
            continue;
        }
        placeObstacle(o);
        if (o.resolved) continue;

        // Angular gap between the ship and the block's nearest edge.
        float centre = ((float)o.lane + (float)(o.lanes - 1) * 0.5f) * LANE_DEG;
        float edgeGap = fabsf(deltaDeg(_angle, centre)) - (float)o.lanes * LANE_DEG * 0.5f;

        bool overlapping = fabsf(z - SHIP_Z) < (BLOCK_DEPTH + SHIP_DEPTH) * 0.5f;
        if (overlapping && edgeGap < SHIP_HALF_DEG) {
            hitShip(o, audio);
        } else if (z < SHIP_Z - (BLOCK_DEPTH + SHIP_DEPTH) * 0.5f) {
            // Passed it cleanly.
            o.resolved = true;
            if (edgeGap < SHIP_HALF_DEG + NEAR_MISS_DEG) {
                _bonus += NEAR_MISS_POINTS;
                _nearMissUntil = millis() + NEAR_MISS_SHOW_MS;
                audio.playTone(1400, 30);
            }
        }
    }
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
    audio.playTone(140, 160);
}

}  // namespace tubeflux
