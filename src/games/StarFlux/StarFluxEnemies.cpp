#include "StarFluxGame.h"

// Fighters and their flight paths, enemy shots, and the boss.

namespace starflux {

namespace {

// A flight path: keys in camera-relative space (see StarFluxConfig.h),
// passed through with a Catmull-Rom spline, and the times it fires.
struct Key { uint16_t ms; int16_t x, y, z; };
struct PathDef {
    const Key* keys;
    uint8_t    count;
    uint16_t   fire[3];      // ms into the path; 0 = none
    uint16_t   stagger;      // ms between one formation slot and the next
};

// V formation diving from high ahead, across your front, breaking up and away.
const Key VDIVE[] = { { 0, 0, 1100, 6000 }, { 1300, -200, 420, 3200 }, { 2400, -250, 60, 2000 },
                      { 3400, -60, -40, 1600 }, { 4400, 180, 120, 1700 }, { 5400, -120, 260, 1800 },
                      { 6300, 400, 1000, 2300 }, { 7200, 900, 2000, 3400 } };
// A column crossing in front of you from one side to the other.
const Key SWEEP[] = { { 0, -1800, 300, 2600 }, { 1500, -800, 120, 1900 }, { 2900, 0, 0, 1500 },
                      { 4300, 800, 140, 1700 }, { 5800, 1800, 380, 2400 } };
// Pairs coming straight at you and past, close either side.
const Key HEADON[] = { { 0, 260, 120, 7000 }, { 1600, 240, 80, 3800 }, { 2800, 300, 40, 2000 },
                       { 3500, 440, -40, 1000 }, { 4100, 720, -160, 150 }, { 4500, 900, -250, -300 } };
// From behind: overtaking wide on one side, turning back for a pass, then off.
const Key LOOP[] = { { 0, -800, -450, -400 }, { 900, -640, -300, 600 }, { 2000, -250, 50, 2000 },
                     { 3000, 200, 260, 2900 }, { 4000, 350, 150, 2300 }, { 5000, 100, -50, 1700 },
                     { 6000, -600, 500, 1900 }, { 6800, -1400, 1100, 2600 } };
// A snake weaving in, holding in front of you, then climbing away.
const Key WEAVE[] = { { 0, 0, 250, 6500 }, { 1200, -450, 120, 4200 }, { 2400, 420, 0, 2800 },
                      { 3500, -320, -40, 2000 }, { 4600, 260, 150, 1800 }, { 5600, 0, 350, 2000 },
                      { 6600, 0, 1400, 3000 } };

const PathDef PATHS[] = {
    { VDIVE,  sizeof(VDIVE) / sizeof(Key),  { 2200, 3800, 5200 }, 120 },
    { SWEEP,  sizeof(SWEEP) / sizeof(Key),  { 1900, 3500, 0 },    FORMATION_STAGGER_MS },
    { HEADON, sizeof(HEADON) / sizeof(Key), { 1900, 0, 0 },       900 },
    { LOOP,   sizeof(LOOP) / sizeof(Key),   { 3300, 4600, 0 },    450 },
    { WEAVE,  sizeof(WEAVE) / sizeof(Key),  { 2600, 4000, 5200 }, 380 },
};

inline float catmull(float p0, float p1, float p2, float p3, float t) {
    float t2 = t * t, t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

}  // namespace

unsigned long StarFluxGame::patternLength(Pattern p) const {
    const PathDef &d = PATHS[p];
    return d.keys[d.count - 1].ms;
}

unsigned long StarFluxGame::patternStagger(Pattern p) const { return PATHS[p].stagger; }

void StarFluxGame::pathPoint(const Fighter &f, unsigned long ms, float &x, float &y, float &z) const {
    const PathDef &d = PATHS[f.pattern];
    int i = 0;
    while (i < d.count - 2 && ms >= d.keys[i + 1].ms) ++i;
    const Key &k1 = d.keys[i], &k2 = d.keys[i + 1];
    const Key &k0 = d.keys[i > 0 ? i - 1 : 0];
    const Key &k3 = d.keys[i + 2 < d.count ? i + 2 : d.count - 1];
    float t = clampf((float)((long)ms - (long)k1.ms) / (float)(k2.ms - k1.ms), 0.0f, 1.0f);
    x = catmull(k0.x, k1.x, k2.x, k3.x, t) * (float)f.mirror + f.ox;
    y = catmull(k0.y, k1.y, k2.y, k3.y, t) + f.oy;
    z = catmull(k0.z, k1.z, k2.z, k3.z, t);
}

// The whole wave at once, each fighter waiting its turn (startAt). V
// formations fly as one; the rest follow each other down the path.
void StarFluxGame::spawnWaveFighters() {
    const Segment &s = segment();
    const PathDef &d = PATHS[s.pattern];
    int n = 0;
    for (auto &f : _fighters) {
        if (n >= s.count) break;
        if (f.active) continue;
        f.active = true;
        f.pattern = s.pattern;
        f.nextFire = 0;
        f.wave = (uint8_t)_seg;
        f.mirror = s.mirror != 0 ? s.mirror : ((n & 1) ? -1 : 1);
        f.startAt = millis() + 400 + (unsigned long)n * d.stagger;
        f.ox = f.oy = 0;
        switch (s.pattern) {
            case PAT_VDIVE: {
                int rank = (n + 1) / 2;
                f.ox = (n & 1 ? -1.0f : 1.0f) * (float)rank * 170.0f;
                f.oy = (float)rank * 110.0f;
                f.startAt = millis() + 400 + (unsigned long)rank * d.stagger;
                break;
            }
            case PAT_HEADON:
                f.mirror = (n & 1) ? -1 : 1;                 // one each side
                f.startAt = millis() + 400 + (unsigned long)(n / 2) * d.stagger;
                f.oy = (float)((n / 2) % 2) * 90.0f;
                break;
            default:
                f.oy = (float)(n % 2) * 60.0f;
                break;
        }
        pathPoint(f, 0, f.x, f.y, f.z);
        f.obj->enabled = false;
        ++n;
    }
    _segSpawned = n;
    _waveLeft[_seg] = (uint8_t)n;
    _waveClean[_seg] = true;
    _fightersSeen += n;
}

// Along their paths, facing where they're going (their speed through the
// world includes the stage's, so one holding station ahead shows its
// tail, and one rushing you shows its nose), banking into turns.
void StarFluxGame::updateFighters(AudioEngine &audio) {
    for (auto &f : _fighters) {
        if (!f.active) continue;
        if (!reached(f.startAt)) { f.obj->enabled = false; continue; }
        unsigned long t = millis() - f.startAt;
        if (t >= patternLength(f.pattern)) {   // got away
            f.active = false;
            f.obj->enabled = false;
            fighterGone(f, false, audio);
            continue;
        }
        pathPoint(f, t, f.x, f.y, f.z);
        float nx, ny, nz;
        pathPoint(f, t + 60, nx, ny, nz);
        float vx = nx - f.x, vy = ny - f.y, vz = nz - f.z + FLY_SPEED * 60.0f / (float)REFERENCE_FRAME_MS;
        float yaw = degrees(atan2f(vx, vz));
        float pitch = degrees(atan2f(vy, sqrtf(vx * vx + vz * vz)));
        float roll = clampf(-vx * 0.35f, -55.0f, 55.0f);
        f.obj->setRotation((int32_t)(PITCH_SIGN * pitch), (int32_t)(YAW_SIGN * yaw), (int32_t)(ROLL_SIGN * roll));
        f.obj->setPosition((int32_t)f.x, (int32_t)f.y, (int32_t)f.z);
        f.obj->enabled = f.z > CAMERA_NEAR + 60;

        const PathDef &d = PATHS[f.pattern];
        if (f.nextFire < 3 && d.fire[f.nextFire] && t >= d.fire[f.nextFire]) {
            ++f.nextFire;
            if (_stage == STAGE_RUN && f.z > MIN_FIRE_Z && random(0, 100) < firePct()) {
                // Some lead you: aimed where you'll be if you keep going.
                float tx = _shipX, ty = _shipY;
                if (random(0, 100) < LEAD_PCT) {
                    float frames = (f.z - SHIP_Z) / eshotSpeed();
                    tx += _shipVX * frames;
                    ty += _shipVY * frames;
                }
                fireAt(f.x, f.y, f.z, tx, ty);
            }
        }
        // Flying through you.
        if (_stage == STAGE_RUN && fabsf(f.z - SHIP_Z) < 110.0f) {
            float dx = f.x - _shipX, dy = f.y - _shipY, rr = FIGHTER_R * 0.6f + SHIP_HIT_R;
            if (dx * dx + dy * dy < rr * rr && !before(_invulnUntil)) {
                destroyFighter(f, false, audio);
                damageShip(RAM_DAMAGE, audio);
            }
        }
    }
}

void StarFluxGame::destroyFighter(Fighter &f, bool byPlayer, AudioEngine &audio) {
    f.active = false;
    f.obj->enabled = false;
    addBlast(f.x, f.y, f.z, 170.0f, ArcadeConfig::COLOR_ORANGE);
    _particles.emitSparks(Renderer::Vec3f{ f.x, f.y, f.z }, Renderer::Vec3f{ 0, 0, -1 }, 600.0f, 12);
    sfxWAV(audio, "/audio/explosion.wav");
    if (byPlayer) {
        _score += FIGHTER_POINTS;
        ++_fightersDowned;
    }
    fighterGone(f, byPlayer, audio);
}

// Books a fighter out of its wave; the last one out of a wave you shot
// down to the last fighter pays the bonus.
void StarFluxGame::fighterGone(Fighter &f, bool downed, AudioEngine &audio) {
    uint8_t w = f.wave;
    if (!downed) _waveClean[w] = false;
    if (_waveLeft[w] == 0) return;
    if (--_waveLeft[w] == 0 && _waveClean[w]) {
        _score += WAVE_PERFECT_POINTS;
        setBanner("PERFECT WAVE +500", ArcadeConfig::COLOR_CYAN, 1600);
        sfxTone(audio, 1568, 90);
    }
}

// A shot from (x, y, z) at where (tx, ty) is on the ship's plane now.
void StarFluxGame::fireAt(float x, float y, float z, float tx, float ty, float speedMul) {
    for (auto &e : _eshots) {
        if (e.active) continue;
        float dx = tx - x, dy = ty - y, dz = SHIP_Z - z;
        float len = sqrtf(dx * dx + dy * dy + dz * dz);
        if (len < 1.0f) return;
        float k = eshotSpeed() * speedMul / len;
        e.active = true;
        e.x = x; e.y = y; e.z = z;
        e.vx = dx * k; e.vy = dy * k; e.vz = dz * k;
        return;
    }
}

void StarFluxGame::updateEShots(AudioEngine &audio) {
    const float fs = _frameScale;
    for (auto &e : _eshots) {
        if (!e.active) continue;
        float pz = e.z;
        e.x += e.vx * fs; e.y += e.vy * fs; e.z += e.vz * fs;
        // Crossing the ship's plane: where, exactly?
        if (pz >= SHIP_Z && e.z < SHIP_Z) {
            float t = (pz - SHIP_Z) / (pz - e.z);
            float cx = e.x - e.vx * fs * (1.0f - t), cy = e.y - e.vy * fs * (1.0f - t);
            float dx = cx - _shipX, dy = cy - _shipY;
            if (_stage == STAGE_RUN && dx * dx + dy * dy < SHIP_HIT_R * SHIP_HIT_R && !before(_invulnUntil)) {
                e.active = false;
                damageShip(SHOT_DAMAGE, audio);
                continue;
            }
        }
        if (e.z < CAMERA_NEAR + 5 || e.z > CAMERA_FAR) e.active = false;
    }
}

// --- Boss --------------------------------------------------------------------
// The dreadnought: two wing cannons and a core behind a shield plate. The
// cannons fire aimed shots; lose one and it adds ring bursts (stay put:
// the ring closes round where you were, not on it); lose both and the
// core opens, firing spreads and faster bursts. Destroy the core to win.

void StarFluxGame::startBoss() {
    _bossActive = true;
    _bossAt = millis();
    _bossZ = BOSS_ENTER_Z;
    _bossAlarms = 0;
    if (!_retrying || (_cannonHp[0] <= 0 && _cannonHp[1] <= 0 && _coreHp <= 0)) {
        float k = 1.0f + 0.25f * (float)(_loop - 1);
        _cannonHp[0] = _cannonHp[1] = (int)(CANNON_HP * k);
        _coreHp = (int)(CORE_HP * k);
        _bossMaxHp = bossHp();
    }
    unsigned long start = millis() + BOSS_ENTER_MS;
    _cannonFireAt = start + 600;
    _burstAt = start + 1500;
    _coreFireAt = start + 900;
    _cannonFlash[0] = _cannonFlash[1] = _coreFlash = 0;
    setBanner("WARNING", ArcadeConfig::COLOR_RED, BOSS_ENTER_MS);
    placeBoss();
}

void StarFluxGame::bossPartPos(int part, float &x, float &y, float &z) const {
    if (part < 2) {
        x = _bossX + (part == 0 ? -CANNON_X : CANNON_X);
        y = _bossY - 15.0f;
        z = _bossZ - 70.0f;
    } else {
        x = _bossX;
        y = _bossY;
        z = _bossZ - 480.0f;
    }
}

bool StarFluxGame::bossPartAlive(int part) const {
    if (part < 2) return _cannonHp[part] > 0;
    return coreOpen() && _coreHp > 0;
}

void StarFluxGame::placeBoss() {
    if (!_bossHull) return;
    const unsigned long now = millis();
    float sway = sinf((float)(now - _bossAt) * 0.0011f);
    _bossHull->enabled = true;
    _bossHull->setPosition((int32_t)_bossX, (int32_t)_bossY, (int32_t)_bossZ);
    _bossHull->setRotation(0, 0, (int32_t)(ROLL_SIGN * sway * 8.0f));
    int spin = (int)((now / 12) % 360);
    for (int p = 0; p < 2; ++p) {
        Renderer::Object* c = _cannonObj[p];
        c->enabled = _cannonHp[p] > 0;
        float x, y, z;
        bossPartPos(p, x, y, z);
        c->setPosition((int32_t)x, (int32_t)y, (int32_t)z);
        c->setRotation(0, spin, 0);
        bool flash = before(_cannonFlash[p]);
        if (flash != (c->triangles[0].material == &_flashMat)) setFlash(c, flash, &_cannonMat, &_cannonMat2);
    }
    float x, y, z;
    bossPartPos(2, x, y, z);
    _coreObj->enabled = _coreHp > 0;
    _coreObj->setPosition((int32_t)x, (int32_t)y, (int32_t)z);
    _coreObj->setRotation(spin, spin * 2, 0);
    bool flash = before(_coreFlash);
    if (flash != (_coreObj->triangles[0].material == &_flashMat)) setFlash(_coreObj, flash, &_coreMat, &_coreMat2);
    _shieldObj->enabled = !coreOpen();
    _shieldObj->setPosition((int32_t)x, (int32_t)y, (int32_t)(z - 100.0f));
}

void StarFluxGame::hideBoss() {
    _bossActive = false;
    if (_bossHull) _bossHull->enabled = false;
    for (auto* c : _cannonObj) if (c) c->enabled = false;
    if (_coreObj) _coreObj->enabled = false;
    if (_shieldObj) _shieldObj->enabled = false;
}

// BURST_SHOTS shots aimed at a ring BURST_RADIUS round where you are now.
void StarFluxGame::ringBurst(float x, float y, float z) {
    for (int i = 0; i < BURST_SHOTS; ++i) {
        float a = (float)i * (2.0f * PI / (float)BURST_SHOTS);
        fireAt(x, y, z, _shipX + cosf(a) * BURST_RADIUS, _shipY + sinf(a) * BURST_RADIUS, 0.9f);
    }
}

void StarFluxGame::updateBoss(AudioEngine &audio) {
    if (!_bossActive) return;
    const unsigned long t = millis() - _bossAt;
    if (t < BOSS_ENTER_MS) {
        float k = (float)t / (float)BOSS_ENTER_MS;
        k = k * k * (3.0f - 2.0f * k);
        _bossZ = BOSS_ENTER_Z + (BOSS_Z - BOSS_ENTER_Z) * k;
        if (_bossAlarms < 3 && t >= (unsigned long)_bossAlarms * 450) {
            ++_bossAlarms;
            sfxTone(audio, 880, 160);
        }
    } else if (_stage == STAGE_BOSS_DEATH) {
        _bossZ += 6.0f * _frameScale;   // sinking away as it breaks up
        _bossY -= 3.0f * _frameScale;
    } else {
        _bossZ = BOSS_Z;
    }
    if (_stage != STAGE_BOSS_DEATH) {
        const float pace = coreOpen() ? 1.6f : 1.0f;
        _bossX = BOSS_SWAY_X * sinf((float)t * 0.00055f * pace);
        _bossY = BOSS_BASE_Y + BOSS_SWAY_Y * sinf((float)t * 0.0009f * pace);
    }
    placeBoss();
    if (t < BOSS_ENTER_MS || _stage != STAGE_RUN) return;

    const bool oneLost = _cannonHp[0] <= 0 || _cannonHp[1] <= 0;
    if (!coreOpen() && reached(_cannonFireAt)) {
        int p = _cannonTurn ^= 1;
        if (_cannonHp[p] <= 0) p ^= 1;
        float x, y, z;
        bossPartPos(p, x, y, z);
        fireAt(x, y, z, _shipX, _shipY, 1.1f);
        sfxTone(audio, 300, 40);
        _cannonFireAt = millis() + (oneLost ? CANNON_FIRE_MS * 3 / 4 : CANNON_FIRE_MS);
    }
    if (oneLost && reached(_burstAt)) {
        float x, y, z;
        bossPartPos(2, x, y, z);
        ringBurst(x, y, z);
        sfxTone(audio, 200, 120);
        _burstAt = millis() + (coreOpen() ? CORE_BURST_MS : RING_BURST_MS);
    }
    if (coreOpen() && reached(_coreFireAt)) {
        float x, y, z;
        bossPartPos(2, x, y, z);
        for (int i = -1; i <= 1; ++i) fireAt(x, y, z, _shipX + (float)i * 130.0f, _shipY, 1.15f);
        sfxTone(audio, 420, 50);
        _coreFireAt = millis() + CORE_FIRE_MS;
    }
}

void StarFluxGame::hitBossPart(int part, int damage, AudioEngine &audio) {
    float x, y, z;
    bossPartPos(part, x, y, z);
    _particles.emitSparks(Renderer::Vec3f{ x, y, z - 60.0f }, Renderer::Vec3f{ 0, 0, -1 }, 300.0f, 4);
    if (part < 2) {
        _cannonHp[part] -= damage;
        _cannonFlash[part] = millis() + 80;
        sfxTone(audio, 700, 25);
        if (_cannonHp[part] > 0) return;
        _cannonHp[part] = 0;
        _score += CANNON_POINTS;
        addBlast(x, y, z, 420.0f, ArcadeConfig::COLOR_YELLOW);
        _particles.emitSparks(Renderer::Vec3f{ x, y, z }, Renderer::Vec3f{ 0, 0, -1 }, 800.0f, 24);
        sfxWAV(audio, "/audio/explosion.wav");
        if (coreOpen()) {
            setBanner("CORE EXPOSED", ArcadeConfig::COLOR_MAGENTA, 1800);
            _coreFireAt = millis() + 900;
            _burstAt = millis() + 1600;
            addBlast(_bossX, _bossY, _bossZ - 520.0f, 260.0f, ArcadeConfig::COLOR_WHITE);
        } else {
            setBanner("CANNON DOWN", ArcadeConfig::COLOR_YELLOW, 1400);
            _burstAt = millis() + 1200;
        }
        return;
    }
    _coreHp -= damage;
    _coreFlash = millis() + 80;
    sfxTone(audio, 950, 25);
    if (_coreHp > 0) return;
    _coreHp = 0;
    _score += CORE_POINTS;
    for (auto &e : _eshots) e.active = false;
    _stage = STAGE_BOSS_DEATH;
    _stageAt = millis();
    _nextBossBlastAt = 0;
    _invulnUntil = millis() + BOSS_DEATH_MS + 500;
    setBanner("DREADNOUGHT DESTROYED", ArcadeConfig::COLOR_CYAN, BOSS_DEATH_MS);
    sfxWAV(audio, "/audio/explosion.wav");
}

}  // namespace starflux
