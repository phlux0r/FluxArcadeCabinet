#include "StarFluxGame.h"

// The ship, its lasers and bombs, rocks and rings, and the stage scripts
// that run it all.

namespace starflux {

namespace {

constexpr int SEGMENTS = 10;   // every stage's script has this many

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

}  // namespace

// Each stage around 90 seconds: fighter waves between hazard fields, three
// shield rings, then the boss.
const StarFluxGame::Segment& StarFluxGame::segment() const {
    static const Segment scripts[STAGE_COUNT][SEGMENTS] = {
        {   // 1, Aurora Belt: rock fields in space; the dreadnought.
            //  type       pattern     count mirror  ms    ring
            { SEG_WAVE,  PAT_VDIVE,   5,  1,     0,    false },
            { SEG_FIELD, PAT_VDIVE,   0,  0,  6000,    true  },
            { SEG_WAVE,  PAT_SWEEP,   6,  0,     0,    false },
            { SEG_WAVE,  PAT_HEADON,  6,  1,     0,    false },
            { SEG_FIELD, PAT_VDIVE,   0,  0,  7000,    false },
            { SEG_WAVE,  PAT_LOOP,    6,  1,     0,    true  },
            { SEG_WAVE,  PAT_WEAVE,   6, -1,     0,    false },
            { SEG_FIELD, PAT_VDIVE,   0,  0,  8000,    true  },
            { SEG_WAVE,  PAT_VDIVE,   7, -1,     0,    false },
            { SEG_BOSS,  PAT_VDIVE,   0,  0,     0,    false },
        },
        {   // 2, Ember Reach: pillars and turret towers on the ground; the crawler.
            { SEG_WAVE,  PAT_SWEEP,   6,  0,     0,    false },
            { SEG_FIELD, PAT_VDIVE,   0,  0,  7000,    true  },
            { SEG_WAVE,  PAT_VDIVE,   7,  1,     0,    false },
            { SEG_WAVE,  PAT_HEADON,  6,  1,     0,    false },
            { SEG_FIELD, PAT_VDIVE,   0,  0,  8000,    false },
            { SEG_WAVE,  PAT_WEAVE,   6,  1,     0,    true  },
            { SEG_WAVE,  PAT_LOOP,    6, -1,     0,    false },
            { SEG_FIELD, PAT_VDIVE,   0,  0,  7000,    true  },
            { SEG_WAVE,  PAT_SWEEP,   6,  0,     0,    false },
            { SEG_BOSS,  PAT_VDIVE,   0,  0,     0,    false },
        },
        {   // 3, Trench Run: barriers, laser gates and turrets; the reactor.
            { SEG_FIELD, PAT_VDIVE,   0,  0,  6000,    false },
            { SEG_WAVE,  PAT_VDIVE,   6,  1,     0,    false },
            { SEG_FIELD, PAT_VDIVE,   0,  0,  7000,    true  },
            { SEG_WAVE,  PAT_HEADON,  6,  1,     0,    false },
            { SEG_FIELD, PAT_VDIVE,   0,  0,  8000,    false },
            { SEG_WAVE,  PAT_WEAVE,   6, -1,     0,    true  },
            { SEG_WAVE,  PAT_SWEEP,   6,  0,     0,    false },
            { SEG_FIELD, PAT_VDIVE,   0,  0,  6000,    true  },
            { SEG_WAVE,  PAT_LOOP,    6,  1,     0,    false },
            { SEG_BOSS,  PAT_VDIVE,   0,  0,     0,    false },
        },
    };
    return scripts[_stageNum][_seg];
}

int StarFluxGame::segmentCount() const { return SEGMENTS; }

void StarFluxGame::startSegment(int index) {
    _seg = index;
    _segAt = millis();
    _segSpawned = 0;
    _nextFieldAt = millis() + 600;
    _fieldCount = 0;
    _segRingDone = !segment().ring;
    _segPodDone = false;
    switch (segment().type) {
        case SEG_WAVE:
            _waveLeft[_seg] = 0;
            _waveToCome[_seg] = segment().count;
            _waveClean[_seg] = true;
            _segLaunchedAt = millis();
            spawnWaveFighters();
            break;
        case SEG_BOSS:  startBoss(); break;
        default: break;
    }
}

bool StarFluxGame::segmentDone() const {
    const Segment &s = segment();
    switch (s.type) {
        // A wave is done once all of it has launched (or WAVE_LAUNCH_MS has
        // gone by: fighters still out from the last wave may hold the slots
        // it needs), and either it's all gone or its last fighter is most of
        // the way through its run: the next wave overlaps its exit.
        case SEG_WAVE: {
            if (millis() - _segAt < 1000) return false;
            if (_segSpawned < s.count && millis() - _segAt < WAVE_LAUNCH_MS) return false;
            if (_waveLeft[_seg] == 0) return true;
            return millis() - _segLaunchedAt > patternStagger(s.pattern) + patternLength(s.pattern) * 3 / 4;
        }
        case SEG_FIELD:
            return millis() - _segAt >= (unsigned long)s.lengthMs + 1500UL;   // the last hazards are still coming
        default:
            return false;   // the boss ends the stage itself (StarFluxBoss.cpp)
    }
}

// A field's next hazard, when it's due: rocks in space, pillars and turret
// towers on the planet, barriers, gates and turrets in the trench. Denser
// each loop.
void StarFluxGame::spawnField(AudioEngine &audio) {
    const Segment &s = segment();
    if (s.type != SEG_FIELD || millis() - _segAt >= s.lengthMs || !reached(_nextFieldAt)) return;
    const unsigned long quicker = (unsigned long)min(250, 40 * (_loop - 1));
    switch (_stageNum) {
        case STAGE_BELT:
            spawnRock(random(0, 100) < ROCK_AIMED_PCT);
            _nextFieldAt = millis() + ROCK_SPAWN_MS - min(quicker, 200UL);
            break;
        case STAGE_PLANET:
            spawnPlanetHazard();
            _nextFieldAt = millis() + PLANET_FIELD_MS - quicker;
            break;
        default:
            spawnTrenchHazard();
            _nextFieldAt = millis() + TRENCH_FIELD_MS - quicker;
            break;
    }
    ++_fieldCount;
}

void StarFluxGame::updateStage(AudioEngine &audio) {
    switch (_stage) {
        case STAGE_INTRO:
            if (millis() - _stageAt >= INTRO_MS) {
                _stage = STAGE_RUN;
                _stageAt = millis();
                startSegment(_seg);
            }
            break;

        case STAGE_RUN:
            spawnField(audio);
            // A wave launches the fighters that didn't fit as slots free up.
            if (segment().type == SEG_WAVE && _segSpawned < segment().count) {
                if (millis() - _segAt < WAVE_LAUNCH_MS) {
                    spawnWaveFighters();
                } else if (_waveToCome[_seg]) {
                    _waveToCome[_seg] = 0;       // out of time: the rest aren't coming
                    _waveClean[_seg] = false;    // and it's no longer a whole wave to down
                }
            }
            if (!_segRingDone && millis() - _segAt > 1200) {
                spawnRing();
                _segRingDone = true;
            }
            if (!_segPodDone && millis() - _segAt > POD_AFTER_MS) {
                _segPodDone = true;
                if (_loop > 1 && !_rapid && (_seg == POD_SEG_A || _seg == POD_SEG_B)) spawnPod();
            }
            if (segmentDone() && _seg + 1 < segmentCount()) startSegment(_seg + 1);
            break;

        case STAGE_DOWN:
            if (millis() - _stageAt >= DOWN_MS) {
                if (_lives <= 0) {
                    if (inDemo()) _demoUntil = millis();   // updateDemo() ends it
                    else enterGameOver(audio);
                } else {
                    retrySegment();
                }
            }
            break;

        case STAGE_BOSS_DEATH: {
            // A string of explosions across the hull, then the results.
            float hx, hy, hz, hr;
            bossHullSphere(hx, hy, hz, hr);
            if (reached(_nextBossBlastAt)) {
                _nextBossBlastAt = millis() + 140;
                float x = hx + (float)random(-(long)hr, (long)hr + 1) * 1.4f, y = hy + (float)random(-(long)hr, (long)hr + 1) * 0.4f;
                float z = hz + (float)random(-300, 301);
                addBlast(x, y, z, (float)random(180, 360), random(0, 2) ? ArcadeConfig::COLOR_ORANGE : ArcadeConfig::COLOR_YELLOW);
                _particles.emitSparks(Renderer::Vec3f{ x, y, z }, Renderer::Vec3f{ 0, 0, -1 }, 600.0f, 10);
                sfxTone(audio, (int)random(90, 260), 60);
            }
            if (millis() - _stageAt >= BOSS_DEATH_MS) {
                addBlast(hx, hy, hz, 1100.0f, ArcadeConfig::COLOR_WHITE);
                sfx(audio, SFX_BOSS_DIE);
                hideBoss();
                if (inDemo()) {
                    _demoUntil = millis();   // updateDemo() ends it
                    _stage = STAGE_RUN;
                } else {
                    enterResults(audio);
                }
            }
            break;
        }
    }
}

// Shot down with lives left: the segment again, from its start, with full
// shield. A boss keeps the damage you'd done it.
void StarFluxGame::retrySegment() {
    for (auto &s : _shots) s.active = false;
    for (auto &e : _eshots) e.active = false;
    for (auto &f : _fighters) { f.active = false; if (f.obj) f.obj->enabled = false; }
    for (auto &r : _rocks) { r.active = false; if (r.obj) r.obj->enabled = false; }
    for (auto &r : _rings) r.active = false;
    _pod.active = false;
    hideWorld();
    _bombActive = false;
    _shield = SHIELD_MAX;
    if (_bombs < BOMBS_START) _bombs = BOMBS_START;
    _shipX = 0; _shipY = 0; _shipVX = _shipVY = 0; _bank = 0;
    _stage = STAGE_RUN;
    _stageAt = millis();
    _invulnUntil = millis() + SPAWN_INVULN_MS;
    _retrying = true;
    startSegment(_seg);
    _retrying = false;
    setBanner("READY", ArcadeConfig::COLOR_WHITE, 1500);
}

// Stick moves the ship round its box, eased so it has a little weight;
// the bank follows the sideways speed. In the fly-in it glides up from the
// bottom on its own.
void StarFluxGame::updateShip(const InputState &input) {
    const float fs = _frameScale;
    float ix = 0, iy = 0;
    if (_stage == STAGE_RUN && (_phase == PHASE_PLAYING || inDemo())) {
        ix = STEER_X_SIGN * input.joyY;
        iy = STEER_Y_SIGN * input.joyX;
        if (fabsf(ix) < 0.12f) ix = 0;
        if (fabsf(iy) < 0.12f) iy = 0;
    }
    const float k = clampf(SHIP_SMOOTH * fs, 0.0f, 1.0f);
    if (_stage == STAGE_INTRO && _phase == PHASE_PLAYING) {
        _shipVX = 0;
        _shipVY = (0.0f - _shipY) * 0.05f;
    } else if (_stage == STAGE_DOWN) {
        _shipVX = _shipVY = 0;
    } else {
        _shipVX += (ix * SHIP_SPEED - _shipVX) * k;
        _shipVY += (iy * SHIP_SPEED - _shipVY) * k;
    }
    _shipX += _shipVX * fs;
    _shipY += _shipVY * fs;
    if (_shipX < -BOX_X) { _shipX = -BOX_X; _shipVX = 0; }
    if (_shipX >  BOX_X) { _shipX =  BOX_X; _shipVX = 0; }
    if (_shipY < BOX_Y_MIN) { _shipY = BOX_Y_MIN; _shipVY = 0; }
    if (_shipY > BOX_Y_MAX) { _shipY = BOX_Y_MAX; _shipVY = 0; }
    float target = clampf(_shipVX / SHIP_SPEED, -1.0f, 1.0f);
    _bank += (target - _bank) * clampf(0.25f * fs, 0.0f, 1.0f);
}

// A: a shot per press, as fast as you can tap; held, steady fire. Twice
// as fast with the rapid-fire pod.
void StarFluxGame::tryFire(const InputState &input, AudioEngine &audio) {
    const bool pressed = input.btnA && !_prevA;
    _prevA = input.btnA;
    if (!input.btnA) return;
    unsigned long since = millis() - _lastShotAt;
    const unsigned long gap = _rapid ? (pressed ? RAPID_TAP_MS : RAPID_HOLD_MS)
                                     : (pressed ? FIRE_TAP_MS : FIRE_HOLD_MS);
    if (since < gap) return;
    for (auto &s : _shots) {
        if (s.active) continue;
        s.active = true;
        s.x = _shipX; s.y = _shipY;
        s.z = s.pz = SHIP_Z + 60.0f;
        _lastShotAt = millis();
        sfxWAV(audio, "/audio/tube_shot.wav");
        return;
    }
}

// B released within BOMB_TAP_MS of pressing it drops a bomb. Longer is
// on its way to hold-to-quit (StarFluxGame::update()), and drops nothing.
void StarFluxGame::updateBombButton(const InputState &input, AudioEngine &audio) {
    if (input.btnB) {
        if (_btnBDownAt == 0) _btnBDownAt = millis() | 1;
        return;
    }
    if (_btnBDownAt != 0) {
        if (millis() - _btnBDownAt < BOMB_TAP_MS) dropBomb(audio);
        _btnBDownAt = 0;
    }
}

void StarFluxGame::dropBomb(AudioEngine &audio) {
    if (_bombActive) return;
    if (_bombs <= 0) { sfxTone(audio, 180, 60); return; }
    --_bombs;
    _bombActive = true;
    _bombX = _shipX; _bombY = _shipY; _bombZ = SHIP_Z + 80.0f;
    sfxTone(audio, 520, 90);
}

// It flies ahead and blows at BOMB_FUSE_Z, or sooner if it touches anything.
void StarFluxGame::updateBomb(AudioEngine &audio) {
    if (!_bombActive) return;
    _bombZ += BOMB_SPEED * _frameScale;
    bool contact = _bombZ >= BOMB_FUSE_Z;
    auto near = [&](float x, float y, float z, float r) {
        float dx = x - _bombX, dy = y - _bombY, dz = z - _bombZ;
        return dx * dx + dy * dy + dz * dz < r * r;
    };
    for (const auto &f : _fighters) if (!contact && f.active && reached(f.startAt) && near(f.x, f.y, f.z, FIGHTER_R + 60)) contact = true;
    for (const auto &r : _rocks) if (!contact && r.active && near(r.x, r.y, r.z, r.r + 60)) contact = true;
    for (const auto &t : _turrets) if (!contact && t.active && near(t.x, t.y, t.z, TURRET_R + 60)) contact = true;
    if (!contact && _bossActive) {
        float hx, hy, hz, hr;
        bossHullSphere(hx, hy, hz, hr);
        if (near(hx, hy, hz, hr + 200)) contact = true;
    }
    if (contact) detonateBomb(audio);
}

// Everything within BOMB_RADIUS goes, every enemy shot in the air is
// cleared, and boss parts in range take BOMB_BOSS_DAMAGE.
void StarFluxGame::detonateBomb(AudioEngine &audio) {
    _bombActive = false;
    addBlast(_bombX, _bombY, _bombZ, BOMB_RADIUS, ArcadeConfig::COLOR_CYAN);
    _particles.emitSparks(Renderer::Vec3f{ _bombX, _bombY, _bombZ }, Renderer::Vec3f{ 0, 0, -1 }, 900.0f, 30);
    sfx(audio, SFX_BOMB);
    const float r2 = BOMB_RADIUS * BOMB_RADIUS;
    auto inRange = [&](float x, float y, float z, float extra) {
        float dx = x - _bombX, dy = y - _bombY, dz = z - _bombZ, r = BOMB_RADIUS + extra;
        return dx * dx + dy * dy + dz * dz < (extra > 0 ? r * r : r2);
    };
    for (auto &f : _fighters) {
        if (f.active && reached(f.startAt) && f.z > CAMERA_NEAR && inRange(f.x, f.y, f.z, 0)) destroyFighter(f, true, audio);
    }
    for (auto &r : _rocks) {
        if (r.active && inRange(r.x, r.y, r.z, r.r)) destroyRock(r, true, audio);
    }
    for (auto &t : _turrets) {
        if (t.active && inRange(t.x, t.y, t.z, TURRET_R)) destroyTurret(t, true, audio);
    }
    for (auto &e : _eshots) e.active = false;
    if (_bossActive && _stage == STAGE_RUN) {
        for (int p = 0; p < 3; ++p) {
            float x, y, z;
            bossPartPos(p, x, y, z);
            if (bossPartAlive(p) && inRange(x, y, z, 300.0f)) hitBossPart(p, BOMB_BOSS_DAMAGE, audio);
        }
    }
}

// Did shot `s` pass within r (plus the beams' reach) of (x, y, z) this frame?
bool StarFluxGame::shotHits(const Shot &s, float x, float y, float z, float r) const {
    if (z < s.pz - r || z > s.z + r) return false;
    float dx = x - s.x, dy = y - s.y, rr = r + SHOT_HIT_PAD;
    return dx * dx + dy * dy < rr * rr;
}

void StarFluxGame::updateShots(AudioEngine &audio) {
    for (auto &s : _shots) {
        if (!s.active) continue;
        s.pz = s.z;
        s.z += SHOT_SPEED * _frameScale;
        if (s.z > SHIP_Z + SHOT_RANGE) { s.active = false; continue; }

        for (auto &f : _fighters) {
            if (!f.active || !reached(f.startAt) || f.z < CAMERA_NEAR) continue;
            if (shotHits(s, f.x, f.y, f.z, FIGHTER_R)) { destroyFighter(f, true, audio); s.active = false; break; }
        }
        if (!s.active) continue;
        for (auto &r : _rocks) {
            if (!r.active || !shotHits(s, r.x, r.y, r.z, r.r)) continue;
            s.active = false;
            if (--r.hp <= 0) {
                destroyRock(r, true, audio);
            } else {
                r.flashUntil = millis() + 90;
                _particles.emitSparks(Renderer::Vec3f{ s.x, s.y, r.z - r.r }, Renderer::Vec3f{ 0, 0, -1 }, 250.0f, 5);
                sfxTone(audio, 330, 25);
            }
            break;
        }
        if (!s.active) continue;
        for (auto &t : _turrets) {
            if (!t.active || !shotHits(s, t.x, t.y, t.z, TURRET_R)) continue;
            s.active = false;
            if (--t.hp <= 0) {
                destroyTurret(t, true, audio);
            } else {
                t.flashUntil = millis() + 90;
                sfx(audio, SFX_HIT);
            }
            break;
        }
        if (!s.active) continue;
        // Missiles can be shot down.
        for (auto &e : _eshots) {
            if (!e.active || !e.homing) continue;
            if (!shotHits(s, e.x, e.y, e.z, MISSILE_R)) continue;
            e.active = false;
            s.active = false;
            _score += MISSILE_POINTS;
            addBlast(e.x, e.y, e.z, 110.0f, ArcadeConfig::COLOR_YELLOW);
            sfx(audio, SFX_POP);
            break;
        }
        if (!s.active) continue;
        // Solid obstacles stop lasers.
        if (shotBlocked(s)) {
            s.active = false;
            _particles.emitSparks(Renderer::Vec3f{ s.x, s.y, s.z }, Renderer::Vec3f{ 0, 0, -1 }, 150.0f, 2);
            continue;
        }
        if (!_bossActive || _stage != STAGE_RUN) continue;
        for (int p = 0; p < 3 && s.active; ++p) {
            if (!bossPartAlive(p)) continue;
            float x, y, z;
            bossPartPos(p, x, y, z);
            if (!shotHits(s, x, y, z, p == 2 ? CORE_R : CANNON_R)) continue;
            s.active = false;
            // The reactor's core: only through the gap in its shield fan.
            if (p == 2 && fanBlocks(s.x, s.y)) {
                _particles.emitSparks(Renderer::Vec3f{ s.x, s.y, z - 80.0f }, Renderer::Vec3f{ 0, 0, -1 }, 200.0f, 3);
                sfx(audio, SFX_ARMOR);
            } else {
                hitBossPart(p, 1, audio);
            }
        }
        // The armoured hull (and the shield plate) soaks up the rest, but
        // not a shot lined up on a weak point: those may sit behind the
        // hull's front (the crawler's pods do).
        bool lined = false;
        for (int p = 0; p < 3 && !lined; ++p) {
            if (!bossPartAlive(p)) continue;
            float x, y, z, r = p == 2 ? CORE_R : CANNON_R;
            bossPartPos(p, x, y, z);
            lined = (s.x - x) * (s.x - x) + (s.y - y) * (s.y - y) < (r + SHOT_HIT_PAD) * (r + SHOT_HIT_PAD);
        }
        float hx, hy, hz, hr;
        bossHullSphere(hx, hy, hz, hr);
        if (s.active && !lined && shotHits(s, hx, hy, hz, hr)) {
            s.active = false;
            _particles.emitSparks(Renderer::Vec3f{ s.x, s.y, s.z }, Renderer::Vec3f{ 0, 0, -1 }, 200.0f, 3);
            sfx(audio, SFX_ARMOR);
        }
    }
}

// Big rocks in slots 0..ROCK_BIG_SLOTS-1, small in the rest. An aimed one
// is on your current line; the rest are strewn across the field.
void StarFluxGame::spawnRock(bool aimed) {
    const bool big = random(0, 100) < 40;
    int from = big ? 0 : ROCK_BIG_SLOTS, to = big ? ROCK_BIG_SLOTS : ROCK_POOL;
    Rock* r = nullptr;
    for (int i = from; i < to && !r; ++i) if (!_rocks[i].active) r = &_rocks[i];
    for (int i = 0; i < ROCK_POOL && !r; ++i) if (!_rocks[i].active) r = &_rocks[i];
    if (!r) return;
    r->active = true;
    r->hp = r->r >= ROCK_BIG_R ? ROCK_BIG_HP : 1;
    if (aimed) {
        r->x = _shipX + (float)random(-60, 61);
        r->y = _shipY + (float)random(-60, 61);
        r->vx = r->vy = 0;
    } else {
        r->x = (float)random(-(long)ROCK_FIELD_X, (long)ROCK_FIELD_X + 1);
        r->y = (float)random(-(long)ROCK_FIELD_Y, (long)ROCK_FIELD_Y + 1) + 40.0f;
        r->vx = (float)random(-30, 31) * 0.1f;
        r->vy = (float)random(-20, 21) * 0.1f;
    }
    r->z = ROCK_SPAWN_Z;
    r->ax = (float)random(0, 360); r->ay = (float)random(0, 360); r->az = (float)random(0, 360);
    r->sx = (float)random(-40, 41) * 0.1f; r->sy = (float)random(-40, 41) * 0.1f; r->sz = (float)random(-20, 21) * 0.1f;
    r->flashUntil = 0;
    r->obj->enabled = true;
}

void StarFluxGame::updateRocks(AudioEngine &audio) {
    const float fs = _frameScale;
    for (auto &r : _rocks) {
        if (!r.active) continue;
        r.z -= FLY_SPEED * fs;
        r.x += r.vx * fs;
        r.y += r.vy * fs;
        r.ax += r.sx * fs; r.ay += r.sy * fs; r.az += r.sz * fs;
        if (r.z < CAMERA_NEAR + 10) { r.active = false; r.obj->enabled = false; continue; }
        // Passing the ship: collide.
        if (_stage == STAGE_RUN && fabsf(r.z - SHIP_Z) < r.r * 0.6f) {
            float dx = r.x - _shipX, dy = r.y - _shipY, rr = r.r * 0.8f + SHIP_HIT_R;
            if (dx * dx + dy * dy < rr * rr && !before(_invulnUntil)) {
                destroyRock(r, false, audio);
                damageShip(ROCK_DAMAGE, audio);
                continue;
            }
        }
        r.obj->setPosition((int32_t)r.x, (int32_t)r.y, (int32_t)r.z);
        r.obj->setRotation((int32_t)r.ax, (int32_t)r.ay, (int32_t)r.az);
        bool flash = before(r.flashUntil);
        Renderer::Material* m = &_rockMat[(&r - _rocks) & 1];
        if (flash != (r.obj->triangles[0].material == &_flashMat)) setFlash(r.obj, flash, m, m);
    }
}

// A big rock shot apart breaks into two small ones, if there's room.
void StarFluxGame::destroyRock(Rock &r, bool byPlayer, AudioEngine &audio) {
    const bool big = r.r >= ROCK_BIG_R;
    r.active = false;
    r.obj->enabled = false;
    setFlash(r.obj, false, &_rockMat[(&r - _rocks) & 1], &_rockMat[(&r - _rocks) & 1]);
    addBlast(r.x, r.y, r.z, r.r * 1.3f, ArcadeConfig::COLOR_AMBER);
    _particles.emitSparks(Renderer::Vec3f{ r.x, r.y, r.z }, Renderer::Vec3f{ 0, 0, -1 }, 500.0f, big ? 16 : 8);
    sfx(audio, SFX_POP);
    if (!byPlayer) return;
    _score += big ? ROCK_BIG_POINTS : ROCK_SMALL_POINTS;
    ++_targetsDowned;
    if (!big) return;
    int made = 0;
    for (int i = ROCK_BIG_SLOTS; i < ROCK_POOL && made < 2; ++i) {
        Rock &c = _rocks[i];
        if (c.active) continue;
        c.active = true;
        c.hp = 1;
        c.x = r.x + (made ? 70.0f : -70.0f);
        c.y = r.y + (float)random(-40, 41);
        c.z = r.z;
        c.vx = made ? 6.0f : -6.0f;
        c.vy = (float)random(-30, 31) * 0.1f;
        c.sx = (float)random(-60, 61) * 0.1f; c.sy = (float)random(-60, 61) * 0.1f; c.sz = 0;
        c.flashUntil = 0;
        c.obj->enabled = true;
        ++made;
    }
}

// A ring somewhere you can reach, far ahead.
void StarFluxGame::spawnRing() {
    for (auto &r : _rings) {
        if (r.active) continue;
        r.active = true;
        r.resolved = false;
        r.x = (float)random(-(long)(BOX_X - 90), (long)(BOX_X - 90) + 1);
        r.y = (float)random((long)(BOX_Y_MIN + 90), (long)(BOX_Y_MAX - 90) + 1);
        r.z = ROCK_SPAWN_Z;
        return;
    }
}

void StarFluxGame::updateRings(AudioEngine &audio) {
    for (auto &r : _rings) {
        if (!r.active) continue;
        r.z -= FLY_SPEED * _frameScale;
        if (r.z < CAMERA_NEAR + 10) { r.active = false; continue; }
        if (!r.resolved && r.z <= SHIP_Z) {
            r.resolved = true;
            float dx = r.x - _shipX, dy = r.y - _shipY;
            if (_stage == STAGE_RUN && dx * dx + dy * dy < RING_CATCH_R * RING_CATCH_R) {
                _shield = min(SHIELD_MAX, _shield + RING_SHIELD);
                _score += RING_POINTS;
                ++_ringsCaught;
                setBanner("SHIELD UP", ArcadeConfig::COLOR_GREEN, 1200);
                sfx(audio, SFX_RING);
            }
        }
    }
}

// The rapid-fire pod, somewhere you can reach, far ahead.
void StarFluxGame::spawnPod() {
    _pod.active = true;
    _pod.resolved = false;
    _pod.x = (float)random(-(long)(BOX_X - 90), (long)(BOX_X - 90) + 1);
    _pod.y = (float)random((long)(BOX_Y_MIN + 90), (long)(BOX_Y_MAX - 90) + 1);
    _pod.z = ROCK_SPAWN_Z;
}

void StarFluxGame::updatePod(AudioEngine &audio) {
    if (!_pod.active) return;
    _pod.z -= FLY_SPEED * _frameScale;
    if (_pod.z < CAMERA_NEAR + 10) { _pod.active = false; return; }
    if (_pod.resolved || _pod.z > SHIP_Z) return;
    _pod.resolved = true;
    float dx = _pod.x - _shipX, dy = _pod.y - _shipY;
    if (_stage == STAGE_RUN && dx * dx + dy * dy < POD_CATCH_R * POD_CATCH_R) {
        _pod.active = false;
        _rapid = true;
        _score += POD_POINTS;
        setBanner("RAPID FIRE", ArcadeConfig::COLOR_YELLOW, 1500);
        sfx(audio, SFX_POWER);
    }
}

void StarFluxGame::damageShip(int amount, AudioEngine &audio) {
    if (_stage != STAGE_RUN || before(_invulnUntil)) return;
    _shield -= amount;
    _hitFlashUntil = millis() + 160;
    _invulnUntil = millis() + HIT_INVULN_MS;
    _particles.emitSparks(Renderer::Vec3f{ _shipX, _shipY, SHIP_Z }, Renderer::Vec3f{ 0, 0, -1 }, 300.0f, 8);
    if (_shield <= 0) {
        _shield = 0;
        shipDown(audio);
    } else {
        sfxWAV(audio, "/audio/tube_bump.wav");
    }
}

// Shield gone: a life lost. The world flies on for DOWN_MS, then
// updateStage() retries the segment or ends the game.
void StarFluxGame::shipDown(AudioEngine &audio) {
    --_lives;
    _rapid = false;
    _stage = STAGE_DOWN;
    _stageAt = millis();
    _bombActive = false;
    addBlast(_shipX, _shipY, SHIP_Z, 320.0f, ArcadeConfig::COLOR_ORANGE);
    _particles.emitSparks(Renderer::Vec3f{ _shipX, _shipY, SHIP_Z }, Renderer::Vec3f{ 0, 0, 1 }, 800.0f, 30);
    sfxWAV(audio, "/audio/explosion.wav");
}

void StarFluxGame::addBlast(float x, float y, float z, float size, uint16_t colour) {
    Blast* slot = &_blasts[0];
    for (auto &b : _blasts) {
        if (!b.active) { slot = &b; break; }
        if (b.at < slot->at) slot = &b;   // all busy: reuse the oldest
    }
    slot->active = true;
    slot->x = x; slot->y = y; slot->z = z;
    slot->size = size;
    slot->at = millis();
    slot->colour = colour;
}

void StarFluxGame::setBanner(const char* text, uint16_t colour, unsigned long ms) {
    _banner = text;
    _bannerColour = colour;
    _bannerUntil = millis() + ms;
}

}  // namespace starflux
