#include "TubeFluxGame.h"

// The drone chase: see DRONE_* in TubeFluxConfig.h for the design. Phases:
//   PURSUE    behind you: lanes flash red, then a bolt comes down each
//   OVERTAKE  it streaks past overhead to hold station ahead
//   AHEAD     weaving and dropping crystals, in front of your gun
//   ESCAPE    survived DRONE_ESCAPE_MS: it pulls away and the chase ends
// No blocks spawn from the start of PURSUE to the end of the chase.

namespace tubeflux {

namespace {

inline float flyRadius() {
    return TUBE_RADIUS * cosf(radians(LANE_DEG / 2)) - FLY_HEIGHT;
}

}  // namespace

void TubeFluxGame::setChaseBanner(const char* text, uint16_t colour) {
    snprintf(_chaseBanner, sizeof(_chaseBanner), "%s", text);
    _chaseBannerColour = colour;
    _chaseBannerUntil = millis() + CHASE_BANNER_MS;
}

void TubeFluxGame::startChase(AudioEngine &audio) {
    _chase = CHASE_PURSUE;
    _chaseIndex = _chaseCount++;
    _chasePhaseAt = millis();
    _nextVolleyAt = millis() + CHASE_FIRST_SHOT_MS;
    _volleyFireAt = 0;
    _warnLanes = 0;
    _droneMaxHp = DRONE_HP + DRONE_HP_PER_CHASE * _chaseIndex;
    _droneHp = _droneMaxHp;
    setChaseBanner("DRONE BEHIND YOU!", ArcadeConfig::COLOR_RED);
    static const int n[] = { 880, 620, 880, 620, 880 };   // klaxon
    static const int d[] = { 140, 140, 140, 140, 220 };
    audio.playMelody(n, d, 5);
}

void TubeFluxGame::updateChase(AudioEngine &audio) {
    if (_chase == CHASE_NONE) {
        if (_dist >= _nextChaseAt) startChase(audio);
        return;
    }
    const unsigned long inPhase = millis() - _chasePhaseAt;

    switch (_chase) {
        case CHASE_PURSUE:
            if (_volleyFireAt && reached(_volleyFireAt)) {
                fireVolley(audio);
            } else if (!_volleyFireAt && reached(_nextVolleyAt) && inPhase < CHASE_PURSUE_MS) {
                warnVolley(audio);
            }
            // Finish on a quiet moment: never with a warned volley unfired.
            if (inPhase >= CHASE_PURSUE_MS && !_volleyFireAt) {
                _chase = CHASE_OVERTAKE;
                _chasePhaseAt = millis();
                _droneZ = -500.0f;
                _droneAngle = wrapDeg(_angle + 180.0f);   // overhead, on the far wall
                _droneTargetAngle = _droneAngle;
                _droneObj->enabled = true;
                setChaseBanner("DRONE AHEAD: SHOOT IT!", ArcadeConfig::COLOR_ORANGE);
                static const int n[] = { 300, 450, 700, 1100 };   // whoosh past
                static const int d[] = {  60,  60,  60,  120 };
                audio.playMelody(n, d, 4);
            }
            break;

        case CHASE_OVERTAKE: {
            float t = (float)inPhase / (float)CHASE_OVERTAKE_MS;
            if (t >= 1.0f) {
                _chase = CHASE_AHEAD;
                _chasePhaseAt = millis();
                _droneZ = DRONE_AHEAD_Z;
                _droneRetargetAt = millis();
                _droneDropAt = millis() + DRONE_DROP_MS;
            } else {
                float e = 1.0f - (1.0f - t) * (1.0f - t);   // fast past you, easing into place
                _droneZ = -500.0f + (DRONE_AHEAD_Z + 500.0f) * e;
            }
            break;
        }

        case CHASE_AHEAD:
            updateDroneAhead(audio);
            if (inPhase >= DRONE_ESCAPE_MS) {
                _chase = CHASE_ESCAPE;
                _chasePhaseAt = millis();
                setChaseBanner("DRONE ESCAPED", ArcadeConfig::COLOR_GREY);
            }
            break;

        case CHASE_ESCAPE:
            _droneZ += 140.0f * _frameScale;
            if (_droneZ > SPAWN_AHEAD) endChase(false);
            break;

        default:
            break;
    }
    placeDrone();
}

// Warn first: the lane(s) about to be fired down flash red on the walls
// for the warn time, so every bolt can be dodged by someone watching. The
// first lane is always yours, so standing still is never safe. From the
// third chase a second lane, at least two away, fires with it.
void TubeFluxGame::warnVolley(AudioEngine &audio) {
    int mine = laneAt(_angle);
    _warnLanes = (uint8_t)(1u << mine);
    if (_chaseIndex >= DRONE_TWIN_VOLLEY_CHASE) {
        int other = (mine + 2 + (int)random(0, TUBE_SIDES - 3)) % TUBE_SIDES;
        _warnLanes |= (uint8_t)(1u << other);
    }
    long warn = (long)DRONE_WARN_MS - 70L * _chaseIndex;
    if (warn < (long)DRONE_WARN_MS_MIN) warn = DRONE_WARN_MS_MIN;
    long gap = (long)DRONE_SHOT_MS - (long)DRONE_SHOT_MS_STEP * _chaseIndex;
    if (gap < (long)DRONE_SHOT_MS_MIN) gap = DRONE_SHOT_MS_MIN;
    _volleyFireAt = millis() + (unsigned long)warn;
    _nextVolleyAt = millis() + (unsigned long)gap;
    audio.playTone(1200, 60);
}

void TubeFluxGame::fireVolley(AudioEngine &audio) {
    for (int lane = 0; lane < TUBE_SIDES; ++lane) {
        if (!(_warnLanes & (1u << lane))) continue;
        for (auto &b : _bolts) {
            if (b.active) continue;
            b.active = true;
            b.resolved = false;
            b.z = -600.0f;               // behind the camera: it comes from the drone
            b.angle = (float)lane * LANE_DEG;
            b.obj->enabled = true;
            break;
        }
    }
    _warnLanes = 0;
    _volleyFireAt = 0;
    audio.playTone(260, 120);
}

// Bolts overtake you down their lane; one that passes while you're in it
// costs a shield. They keep flying after the chase moves on.
void TubeFluxGame::updateBolts(AudioEngine &audio) {
    const float shipLo = SHIP_Z - SHIP_DEPTH * 0.5f, shipHi = SHIP_Z + SHIP_DEPTH * 0.5f;
    for (auto &b : _bolts) {
        if (!b.active) continue;
        const float from = b.z;
        b.z += DRONE_BOLT_SPEED * _frameScale;
        if (!b.resolved) {
            // Swept: the span the bolt covered this frame against the ship's.
            bool overlaps = from - DRONE_BOLT_DEPTH * 0.5f <= shipHi && b.z + DRONE_BOLT_DEPTH * 0.5f >= shipLo;
            if (overlaps && fabsf(deltaDeg(_angle, b.angle)) < DRONE_BOLT_HALF_DEG + SHIP_HALF_DEG) {
                b.resolved = true;
                damageShip(audio);
            } else if (b.z - DRONE_BOLT_DEPTH * 0.5f > shipHi) {
                b.resolved = true;
            }
        }
        if (b.z > SPAWN_AHEAD) {
            b.active = false;
            b.obj->enabled = false;
            continue;
        }
        float x, y;
        lanePoint(b.angle, flyRadius(), b.z, x, y);
        b.obj->setPosition((int32_t)lroundf(x), (int32_t)lroundf(y), (int32_t)b.z);
    }
}

// Ahead: weave towards a lane near yours (so it can be lined up), re-picked
// every DRONE_RETARGET_MS, and drop a crystal behind it every DRONE_DROP_MS.
void TubeFluxGame::updateDroneAhead(AudioEngine &audio) {
    (void)audio;
    if (reached(_droneRetargetAt)) {
        int lane = (laneAt(_angle) + (int)random(-2, 3) + TUBE_SIDES) % TUBE_SIDES;
        _droneTargetAngle = (float)lane * LANE_DEG;
        _droneRetargetAt = millis() + DRONE_RETARGET_MS;
    }
    float d = deltaDeg(_droneTargetAngle, _droneAngle);
    float step = DRONE_WEAVE_RATE * _frameScale;
    if (fabsf(d) <= step) _droneAngle = _droneTargetAngle;
    else                  _droneAngle = wrapDeg(_droneAngle + (d > 0 ? step : -step));

    if (reached(_droneDropAt)) {
        dropCrystal(laneAt(_droneAngle), _dist + _droneZ);
        _droneDropAt = millis() + DRONE_DROP_MS;
    }
}

// A crystal where the drone is, in its lane. Dropped, not spawned: it
// ignores the safe lane, but there's only ever one at a given depth, so
// it can always be dodged, and shot.
void TubeFluxGame::dropCrystal(int lane, float at) {
    for (auto &c : _crystals) {
        if (c.active) continue;
        c.active = true;
        c.resolved = false;
        c.lane = lane;
        c.at = at;
        c.obj->enabled = true;
        placeObstacle(c);
        return;
    }
}

void TubeFluxGame::placeDrone() {
    if (_chase == CHASE_NONE || _chase == CHASE_PURSUE) return;
    float x, y;
    lanePoint(_droneAngle, TUBE_RADIUS * cosf(radians(LANE_DEG / 2)) - DRONE_HEIGHT, _droneZ, x, y);
    _droneObj->setPosition((int32_t)lroundf(x), (int32_t)lroundf(y), (int32_t)_droneZ);
    _droneObj->setRotation(0, 0, OBSTACLE_ROLL_SIGN * (int32_t)lroundf(_droneAngle));

    const bool flash = before(_droneFlashUntil);
    _droneHullMat.color   = flash ? 0xFFFF : (uint16_t)((30 << 11) | (8 << 5) | 4);    // red
    _droneTopMat.color    = flash ? 0xFFFF : (uint16_t)((31 << 11) | (30 << 5) | 12);  // orange-red, lit
    _droneDarkMat.color   = flash ? 0xFFFF : (uint16_t)((9 << 11) | (20 << 5) | 12);   // gunmetal armour
    _droneEngineMat.color = flash ? 0xFFFF : (((millis() / 60) & 1) ? (uint16_t)((31 << 11) | (52 << 5) | 10)
                                                                    : (uint16_t)((31 << 11) | (36 << 5) | 4));
}

// Did a shot travelling from..to (distances along the run) down `angle`
// meet the drone? Only while it's holding station ahead.
bool TubeFluxGame::droneShotAt(float from, float to, float angle) const {
    if (_chase != CHASE_AHEAD) return false;
    const float at = _dist + _droneZ;
    if (at + DRONE_DEPTH * 0.5f < from || at - DRONE_DEPTH * 0.5f > to) return false;
    return fabsf(deltaDeg(angle, _droneAngle)) < DRONE_HALF_DEG;
}

void TubeFluxGame::hitDrone(AudioEngine &audio) {
    float x, y;
    lanePoint(_droneAngle, TUBE_RADIUS * cosf(radians(LANE_DEG / 2)) - DRONE_HEIGHT, _droneZ, x, y);
    if (--_droneHp > 0) {
        _droneFlashUntil = millis() + DRONE_HIT_FLASH_MS;
        _particles.emitSparks(Renderer::Vec3f{ x, y, _droneZ - DRONE_DEPTH * 0.5f },
                              Renderer::Vec3f{ 0, 0, -1 }, 260.0f, 10);
        audio.playTone(900, 40);
        return;
    }
    // Destroyed.
    for (int i = 0; i < 3; ++i) {
        _particles.emitSparks(Renderer::Vec3f{ x, y, _droneZ }, Renderer::Vec3f{ 0, 0, -1 }, 700.0f, 30);
    }
    int points = DRONE_POINTS + DRONE_POINTS_PER_CHASE * _chaseIndex;
    _bonus += points;
    char buf[24];
    snprintf(buf, sizeof(buf), "DRONE DOWN +%d", points);
    setChaseBanner(buf, ArcadeConfig::COLOR_YELLOW);
    audio.playWAV("/audio/explosion.wav");
    endChase(true);
}

void TubeFluxGame::endChase(bool destroyed) {
    _chase = CHASE_NONE;
    _warnLanes = 0;
    _volleyFireAt = 0;
    _droneObj->enabled = false;
    _nextChaseAt = _dist + CHASE_EVERY;
    if (destroyed) ++_dronesDestroyed;
    else           ++_dronesEscaped;
}

// Everything the chase has on screen, gone: for a new run, game over and
// the attract screen.
void TubeFluxGame::hideChase() {
    _chase = CHASE_NONE;
    _warnLanes = 0;
    _volleyFireAt = 0;
    _chaseBannerUntil = 0;
    if (_droneObj) _droneObj->enabled = false;
    for (auto &b : _bolts) { b.active = false; if (b.obj) b.obj->enabled = false; }
}

}  // namespace tubeflux
