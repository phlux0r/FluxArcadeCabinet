#include "StarFluxGame.h"

// The three bosses. Each has two outer weak points (parts 0 and 1) and a
// core (part 2) behind a shield plate, which opens once both are gone. Each
// fires aimed shots from its outer parts, gains an attack when it loses the
// first, and changes again when the core opens:
//
//   Dreadnought (stage 1): wing cannons; then ring bursts that close round
//     where you were (stay put); then core spreads and faster bursts.
//   Crawler (stage 2): a tracked fortress on the ground. Missile pods; then
//     homing missiles (shoot them down, or outrun them: they stop steering
//     close in); then its dome fires five-way spreads between missiles.
//   Reactor (stage 3): the end of the trench. Emitters; then spiral
//     streams; then the core is shielded by a rotating fan, and only shots
//     through its gap reach it (line up off-centre, where the gap's coming).

namespace starflux {

namespace {
// Per boss: depth it holds at, hull sphere (for soaking up shots and the
// death explosions), relative to the boss's position.
struct BossDef { float z, hullY, hullR; const char* name; const char* downBanner; };
const BossDef BOSSES[3] = {
    { 2000.0f,   0.0f, 330.0f, "DREADNOUGHT", "DREADNOUGHT DESTROYED" },
    { 2300.0f, 300.0f, 480.0f, "CRAWLER",     "CRAWLER DESTROYED" },
    { 2700.0f,   0.0f, 560.0f, "REACTOR",     "REACTOR DESTROYED" },
};

inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
}  // namespace

float StarFluxGame::bossZ() const { return BOSSES[_bossKind].z; }
const char* StarFluxGame::bossName() const { return BOSSES[_bossKind].name; }

void StarFluxGame::bossHullSphere(float &x, float &y, float &z, float &r) const {
    x = _bossX;
    y = _bossY + BOSSES[_bossKind].hullY;
    z = _bossZ;
    r = BOSSES[_bossKind].hullR;
}

void StarFluxGame::startBoss() {
    _bossKind = _stageNum;
    _bossActive = true;
    _bossAt = millis();
    _bossZ = BOSS_ENTER_Z;
    _bossX = 0;
    _bossY = _bossKind == STAGE_PLANET ? GROUND_Y : _bossKind == STAGE_TRENCH ? 40.0f : BOSS_BASE_Y;
    _bossAlarms = 0;
    _fanAngle = 0;
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
    // Weak points in the boss's own colours.
    static const uint16_t partA[3] = { rgb(255, 150, 30), rgb(170, 255, 60), rgb(80, 220, 255) };
    static const uint16_t partB[3] = { rgb(200, 80, 10),  rgb(90, 170, 20),  rgb(20, 120, 200) };
    static const uint16_t coreA[3] = { rgb(255, 60, 200), rgb(255, 70, 50),  rgb(240, 250, 255) };
    static const uint16_t coreB[3] = { rgb(160, 20, 130), rgb(170, 20, 20),  rgb(120, 160, 255) };
    _cannonMat.color = partA[_bossKind];
    _cannonMat2.color = partB[_bossKind];
    _coreMat.color = coreA[_bossKind];
    _coreMat2.color = coreB[_bossKind];
    for (auto* h : _bossHulls) if (h) h->enabled = false;
    _bossHull = _bossHulls[_bossKind];
    setBanner("WARNING", ArcadeConfig::COLOR_RED, BOSS_ENTER_MS);
    placeBoss();
}

void StarFluxGame::bossPartPos(int part, float &x, float &y, float &z) const {
    const float side = part == 0 ? -1.0f : 1.0f;
    switch (_bossKind) {
        case STAGE_PLANET:
            if (part < 2) { x = _bossX + side * 300.0f; y = _bossY + 600.0f; z = _bossZ - 100.0f; }
            else          { x = _bossX;                 y = _bossY + 320.0f; z = _bossZ - 500.0f; }
            break;
        case STAGE_TRENCH:
            if (part < 2) { x = _bossX + side * 400.0f; y = _bossY + 170.0f; z = _bossZ - 260.0f; }
            else          { x = _bossX;                 y = _bossY - 10.0f;  z = _bossZ - 300.0f; }
            break;
        default:
            if (part < 2) { x = _bossX + side * CANNON_X; y = _bossY - 15.0f; z = _bossZ - 70.0f; }
            else          { x = _bossX;                   y = _bossY;         z = _bossZ - 480.0f; }
            break;
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
    _bossHull->setRotation(0, 0, _bossKind == STAGE_BELT ? (int32_t)(ROLL_SIGN * sway * 8.0f) : 0);
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
    for (auto* h : _bossHulls) if (h) h->enabled = false;
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

// The reactor's shield fan: four blades of 60 degrees round the core, and
// a 120-degree gap, turning. Does it stop a shot at (x, y)?
bool StarFluxGame::fanBlocks(float x, float y) const {
    if (_bossKind != STAGE_TRENCH || !coreOpen()) return false;
    float cx, cy, cz;
    bossPartPos(2, cx, cy, cz);
    float dx = x - cx, dy = y - cy;
    if (dx * dx + dy * dy < 25.0f * 25.0f) return true;   // the hub
    float a = degrees(atan2f(dy, dx)) - _fanAngle;
    while (a < 0) a += 360.0f;
    while (a >= 360.0f) a -= 360.0f;
    return a >= FAN_GAP_DEG;   // the gap is the first FAN_GAP_DEG degrees
}

void StarFluxGame::updateBoss(AudioEngine &audio) {
    if (!_bossActive) return;
    const unsigned long t = millis() - _bossAt;
    const float hold = bossZ();
    if (t < BOSS_ENTER_MS) {
        float k = (float)t / (float)BOSS_ENTER_MS;
        k = k * k * (3.0f - 2.0f * k);
        _bossZ = BOSS_ENTER_Z + (hold - BOSS_ENTER_Z) * k;
        if (_bossAlarms == 0) {
            ++_bossAlarms;
            sfx(audio, SFX_BOSS_WARN);
        } else if (!_sfxOnCard[SFX_BOSS_WARN] && _bossAlarms < 3 && t >= (unsigned long)_bossAlarms * 450) {
            ++_bossAlarms;
            sfxTone(audio, 880, 160);   // no alarm on the card: three beeps
        }
    } else if (_stage == STAGE_BOSS_DEATH) {
        _bossZ += 6.0f * _frameScale;   // sinking away as it breaks up
        if (_bossKind != STAGE_PLANET) _bossY -= 3.0f * _frameScale;
    } else {
        _bossZ = hold;
    }
    const float lastX = _bossX;
    if (_stage != STAGE_BOSS_DEATH) {
        const float pace = coreOpen() ? 1.6f : 1.0f;
        switch (_bossKind) {
            case STAGE_PLANET:   // rolls from side to side on its tracks
                _bossX = 180.0f * sinf((float)t * 0.0005f * pace);
                break;
            case STAGE_TRENCH:   // barely moves: it's the end of the trench
                _bossX = 60.0f * sinf((float)t * 0.0007f);
                _bossY = 40.0f + 40.0f * sinf((float)t * 0.0011f);
                break;
            default:
                _bossX = BOSS_SWAY_X * sinf((float)t * 0.00055f * pace);
                _bossY = BOSS_BASE_Y + BOSS_SWAY_Y * sinf((float)t * 0.0009f * pace);
                break;
        }
    }
    _bossVX = _frameScale > 0 ? (_bossX - lastX) / _frameScale : 0;
    if (_bossKind == STAGE_TRENCH && coreOpen()) _fanAngle = fmodf(_fanAngle + FAN_SPIN * _frameScale, 360.0f);
    placeBoss();
    if (t < BOSS_ENTER_MS || _stage != STAGE_RUN) return;
    bossAttacks(audio);
}

void StarFluxGame::bossAttacks(AudioEngine &audio) {
    const bool oneLost = _cannonHp[0] <= 0 || _cannonHp[1] <= 0;
    float cx, cy, cz;
    bossPartPos(2, cx, cy, cz);

    // Aimed fire from the outer parts, alternating, quicker once one's gone.
    if (!coreOpen() && reached(_cannonFireAt)) {
        int p = _cannonTurn ^= 1;
        if (_cannonHp[p] <= 0) p ^= 1;
        float x, y, z;
        bossPartPos(p, x, y, z);
        fireAt(x, y, z, _shipX, _shipY, 1.1f);
        sfx(audio, SFX_BOSS_FIRE);
        _cannonFireAt = millis() + (oneLost ? CANNON_FIRE_MS * 3 / 4 : CANNON_FIRE_MS);
    }

    switch (_bossKind) {
        case STAGE_PLANET:
            // Homing missiles, from a pod still standing or the deck.
            if (oneLost && reached(_burstAt)) {
                int p = _cannonHp[0] > 0 ? 0 : 1;
                float x, y, z;
                bossPartPos(p, x, y, z);
                fireMissile(x, y, z);
                if (coreOpen()) fireMissile(x + 300.0f, y, z);
                sfx(audio, SFX_BURST);
                _burstAt = millis() + (coreOpen() ? 2200 : 2600);
            }
            if (coreOpen() && reached(_coreFireAt)) {
                for (int i = -2; i <= 2; ++i) fireAt(cx, cy, cz, _shipX + (float)i * 150.0f, _shipY, 1.1f);
                sfx(audio, SFX_BOSS_FIRE);
                _coreFireAt = millis() + 1300;
            }
            break;

        case STAGE_TRENCH:
            // Spiral streams: a shot every 110ms, aimed round a turning
            // circle about you, for 1.6s; then a pause.
            if (oneLost && reached(_burstAt)) {
                unsigned long since = millis() - _burstAt;
                const unsigned long stream = 1600, pause = coreOpen() ? 1400 : 2400;
                if (since < stream) {
                    if (reached(_spiralShotAt) || since < 20) {
                        _spiralShotAt = millis() + 110;
                        float a = radians((float)(millis() % 3600) * 0.35f);
                        fireAt(cx, cy, cz, _shipX + cosf(a) * 230.0f, _shipY + sinf(a) * 230.0f, 1.0f);
                        if ((millis() / 110) % 3 == 0) sfx(audio, SFX_BURST);
                    }
                } else {
                    _burstAt = millis() + pause;
                }
            }
            if (coreOpen() && reached(_coreFireAt)) {
                for (int i = -1; i <= 1; ++i) fireAt(cx, cy, cz, _shipX + (float)i * 130.0f, _shipY, 1.15f);
                sfx(audio, SFX_BOSS_FIRE);
                _coreFireAt = millis() + 1500;
            }
            break;

        default:
            if (oneLost && reached(_burstAt)) {
                ringBurst(cx, cy, cz);
                sfx(audio, SFX_BURST);
                _burstAt = millis() + (coreOpen() ? CORE_BURST_MS : RING_BURST_MS);
            }
            if (coreOpen() && reached(_coreFireAt)) {
                for (int i = -1; i <= 1; ++i) fireAt(cx, cy, cz, _shipX + (float)i * 130.0f, _shipY, 1.15f);
                sfx(audio, SFX_BOSS_FIRE);
                _coreFireAt = millis() + CORE_FIRE_MS;
            }
            break;
    }
}

void StarFluxGame::hitBossPart(int part, int damage, AudioEngine &audio) {
    float x, y, z;
    bossPartPos(part, x, y, z);
    _particles.emitSparks(Renderer::Vec3f{ x, y, z - 60.0f }, Renderer::Vec3f{ 0, 0, -1 }, 300.0f, 4);
    if (part < 2) {
        _cannonHp[part] -= damage;
        _cannonFlash[part] = millis() + 80;
        if (_cannonHp[part] > 0) { sfx(audio, SFX_HIT); return; }
        _cannonHp[part] = 0;
        _score += CANNON_POINTS;
        addBlast(x, y, z, 420.0f, ArcadeConfig::COLOR_YELLOW);
        _particles.emitSparks(Renderer::Vec3f{ x, y, z }, Renderer::Vec3f{ 0, 0, -1 }, 800.0f, 24);
        sfx(audio, SFX_PART_DOWN);
        if (coreOpen()) {
            setBanner("CORE EXPOSED", ArcadeConfig::COLOR_MAGENTA, 1800);
            sfx(audio, SFX_CORE_OPEN);
            _coreFireAt = millis() + 900;
            _burstAt = millis() + 1600;
            float cx, cy, cz;
            bossPartPos(2, cx, cy, cz);
            addBlast(cx, cy, cz - 40.0f, 260.0f, ArcadeConfig::COLOR_WHITE);
        } else {
            static const char* const lost[3] = { "CANNON DOWN", "POD DOWN", "EMITTER DOWN" };
            setBanner(lost[_bossKind], ArcadeConfig::COLOR_YELLOW, 1400);
            _burstAt = millis() + 1200;
        }
        return;
    }
    _coreHp -= damage;
    _coreFlash = millis() + 80;
    if (_coreHp > 0) { sfx(audio, SFX_HIT); return; }
    _coreHp = 0;
    _score += CORE_POINTS;
    for (auto &e : _eshots) e.active = false;
    _stage = STAGE_BOSS_DEATH;
    _stageAt = millis();
    _nextBossBlastAt = 0;
    _invulnUntil = millis() + BOSS_DEATH_MS + 500;
    setBanner(BOSSES[_bossKind].downBanner, ArcadeConfig::COLOR_CYAN, BOSS_DEATH_MS);
    sfx(audio, SFX_PART_DOWN);
}

}  // namespace starflux
