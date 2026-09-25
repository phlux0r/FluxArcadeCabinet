#include "TankFluxGame.h"
// The only Tank Flux file that includes the shared samples, so the PROGMEM
// explosion fallback isn't duplicated across translation units.
#include "../../assets/shared/SharedAssets.h"

namespace tankflux {

// --- Queries -------------------------------------------------------------------

// Would putting `self` at (x,z) overlap tank `o`? For a move (not a spawn)
// it only counts if the move also brings them closer, so two tanks that
// already overlap can still drive apart instead of locking up.
bool TankFluxGame::crowdsTank(const Enemy &self, const Enemy &o, float x, float z, bool spawning) const {
    if (!within(x, z, o.x, o.z, self.spec->radius + o.spec->radius)) return false;
    if (spawning) return true;
    float nx = x - o.x, nz = z - o.z;
    float cx = self.x - o.x, cz = self.z - o.z;
    return nx * nx + nz * nz < cx * cx + cz * cz;
}

bool TankFluxGame::blockedByTank(const Enemy &self, float x, float z, bool spawning) const {
    for (const auto &o : _enemies) {
        if (&o != &self && o.alive && crowdsTank(self, o, x, z, spawning)) return true;
    }
    return _bossActive && !isBoss(self) && crowdsTank(self, _boss, x, z, spawning);
}

// Tanks allowed on the field at once: 1 -> 2 -> 3, so the opening is
// survivable while you learn where cover is.
int TankFluxGame::enemyCap() const {
    if (_level <= 2) return 1;
    if (_level <= 4) return 2;
    return MAX_ENEMIES;
}

int TankFluxGame::aliveEnemies() const {
    int n = 0;
    for (const auto &e : _enemies) if (e.alive) ++n;
    return n;
}

float TankFluxGame::enemySpeed() const {
    return ENEMY_SPEED + (float)(_level - 1) * ENEMY_SPEED_PER_LEVEL;
}

// Absolute millis() time of the next allowed shot.
unsigned long TankFluxGame::fireDelay() const {
    unsigned long cut = (unsigned long)(_level - 1) * FIRE_CUT_PER_LEVEL_MS;
    unsigned long lo = (ENEMY_FIRE_MIN_MS > cut + ENEMY_FIRE_FLOOR_MS)
                     ? ENEMY_FIRE_MIN_MS - cut : ENEMY_FIRE_FLOOR_MS;
    unsigned long hi = (ENEMY_FIRE_MAX_MS > cut + ENEMY_FIRE_FLOOR_MS)
                     ? ENEMY_FIRE_MAX_MS - cut : ENEMY_FIRE_FLOOR_MS + ENEMY_FIRE_FLOOR_SPREAD_MS;
    return millis() + (unsigned long)random((long)lo, (long)hi);
}

// Tougher classes only once more than one tank is on the field (enemyCap()).
TankFluxGame::EnemyClass TankFluxGame::pickEnemyClass() const {
    if (_level <= 2) return CLASS_1;
    if (_level <= 4) return (random(0, 2) == 0) ? CLASS_1 : CLASS_2;
    int r = random(0, 3);
    return r == 0 ? CLASS_1 : (r == 1 ? CLASS_2 : CLASS_3);
}

// --- State changes -----------------------------------------------------------

void TankFluxGame::setTankVisible(Enemy &e, bool visible) {
    e.hull->enabled   = visible;
    e.turret->enabled = visible;
    e.barrel->enabled = visible;
    e.trackL->enabled = visible;
    e.trackR->enabled = visible;
}

void TankFluxGame::setBarrelHot(Enemy &e, bool hot) {
    setObjectMaterial(e.barrel, hot ? &_barrelHotMat : e.barrelMat);
}

// Cancels a telegraphed shot or unfinished burst, so a tank never comes back
// with a glowing barrel or a queued shot.
void TankFluxGame::resetFireState(Enemy &e) {
    e.fireAt = 0;
    e.volley = 0;
    e.burstShotsLeft = 0;
    e.playerBumping = false;
    setBarrelHot(e, false);
}

// Spawns on the arena perimeter, clear of obstacles, other tanks and the
// player. If every attempt is blocked, retries SPAWN_RETRY_MS later.
void TankFluxGame::spawnEnemy(Enemy &e) {
    for (int attempt = 0; attempt < SPAWN_ATTEMPTS; ++attempt) {
        float ang = radians((float)random(0, 360));
        float r   = (float)(ARENA_HALF - ENEMY_SPAWN_INSET);
        float ex  = sinf(ang) * r;
        float ez  = cosf(ang) * r;
        if (_arena.blocked(ex, ez, e.spec->radius)) continue;
        if (blockedByTank(e, ex, ez, true)) continue;
        if (within(ex, ez, _x, _z, ENEMY_SPAWN_MIN_DIST)) continue;
        e.x = ex;
        e.z = ez;
        e.headingDeg = bearingTo(ex, ez, _x, _z);
        e.alive = true;
        resetFireState(e);
        e.tankClass = pickEnemyClass();
        e.hp = e.maxHp = CLASS_HP[e.tankClass];
        // Class shows as turret colour (Jet has no per-object scale).
        setObjectMaterial(e.turret, e.tankClass == CLASS_1 ? &_enemyTurretMat
                                   : e.tankClass == CLASS_2 ? &_enemyTurretMatClass2
                                                             : &_enemyTurretMatClass3);
        setTankVisible(e, true);
        e.nextFireAt = fireDelay();
        return;
    }
    e.respawnAt = millis() + SPAWN_RETRY_MS;
}

// Spawns on the far side of the arena from the player (their angle from the
// centre plus 180 degrees, with jitter), so the boss is always seen coming.
// A plain minimum-distance check can't guarantee that: with the player near
// the centre, every perimeter point is about the same distance away.
// Returns false (leaving _bossPending set) if every attempt was blocked.
bool TankFluxGame::trySpawnBoss(AudioEngine &audio) {
    float playerAngle = atan2f(_x, _z);   // same atan2(dx,dz) convention as bearingTo()
    for (int attempt = 0; attempt < SPAWN_ATTEMPTS; ++attempt) {
        float jitter = radians((float)random(-BOSS_SPAWN_JITTER_DEG, BOSS_SPAWN_JITTER_DEG + 1));
        float ang = playerAngle + PI + jitter;
        float r   = (float)(ARENA_HALF - BOSS_SPAWN_INSET);
        float ex  = sinf(ang) * r;
        float ez  = cosf(ang) * r;
        if (_arena.blocked(ex, ez, _boss.spec->radius)) continue;
        if (blockedByTank(_boss, ex, ez, true)) continue;
        if (within(ex, ez, _x, _z, BOSS_MIN_SPAWN_DIST)) continue;
        _boss.x = ex;
        _boss.z = ez;
        _boss.headingDeg = bearingTo(ex, ez, _x, _z);
        _boss.alive = true;
        resetFireState(_boss);
        _bossSpawnedAt = millis();
        _boss.hp = _boss.maxHp = BOSS_HP + BOSS_HP_STEP * _bossesDefeated;
        setTankVisible(_boss, true);
        _boss.nextFireAt = fireDelay();
        _bossActive = true;
        audio.playTone(300, 400);   // low arrival cue
        return true;
    }
    return false;
}

// A hit that doesn't kill gets small sparks and a clang. A heavy hit (boss
// rear armour) gets more sparks and a higher clang, so flanking visibly pays.
void TankFluxGame::hitEnemy(Enemy &e, AudioEngine &audio, int damage) {
    e.hp -= damage;
    if (e.hp <= 0) {
        destroyEnemy(e, audio);
        return;
    }
    bool heavy = damage > 1;
    _particles.emitSparks(Renderer::Vec3f{ e.x, 120.0f, e.z },
                          Renderer::Vec3f{ 0, 1, 0 }, 300.0f, heavy ? 24 : 10);
    audio.playTone(heavy ? 1300 : 650, heavy ? 90 : 50);
}

void TankFluxGame::destroyEnemy(Enemy &e, AudioEngine &audio) {
    const bool boss = isBoss(e);
    _particles.emitSparks(Renderer::Vec3f{ e.x, 120.0f, e.z },
                          Renderer::Vec3f{ 0, 1, 0 }, 520.0f, boss ? 46 : 26);
    e.alive = false;
    resetFireState(e);
    setTankVisible(e, false);
    playExplosion(audio);

    if (boss) {
        // Not counted in _kills/_level: the boss is a detour from regular
        // escalation, and counting it could land on the next trigger.
        _bossActive = false;
        long fightSecs = (long)((millis() - _bossSpawnedAt) / 1000UL);
        _bossBonus = BOSS_TIME_BONUS_MAX - fightSecs * BOSS_TIME_BONUS_PER_SEC;
        if (_bossBonus < 0) _bossBonus = 0;
        _bossBonusUntil = millis() + BOSS_BONUS_SHOW_MS;
        _score += BOSS_SCORE + _bossBonus;
        _bossesDefeated++;
        audio.playTone(1900, 300);   // kill fanfare
        regenerateArena();
        return;
    }

    e.respawnAt = millis() + ENEMY_RESPAWN_MS;
    _score += SCORE_PER_KILL;
    _kills++;
    int newLevel = min(MAX_LEVEL, 1 + _kills / KILLS_PER_LEVEL);
    if (newLevel != _level) {
        _level = newLevel;
        audio.playTone(1900, 140);   // level-up cue
    }
    if (!_bossActive && _kills >= _nextBossAt) {
        _nextBossAt += BOSS_EVERY_KILLS;
        _bossPending = true;
        _bossAlertUntil = millis() + BOSS_ALERT_MS;
        _bossKlaxonAt = millis();
    }
}

// Shared /audio/explosion.wav, with the PROGMEM sample as fallback.
void TankFluxGame::playExplosion(AudioEngine &audio) {
    audio.playExplosionSound(explosion_data, sizeof(explosion_data));
}

// --- Per-frame -----------------------------------------------------------------

void TankFluxGame::updateEnemies(AudioEngine &audio) {
    for (auto &e : _enemies) {
        if (!e.alive) {
            // No backfill during a boss fight.
            if (_bossActive) continue;
            // At the cap, keep pushing the timer back so a kill always buys
            // a breather instead of being replaced the same instant.
            if (aliveEnemies() >= enemyCap()) {
                e.respawnAt = millis() + ENEMY_RESPAWN_MS;
            } else if (reached(e.respawnAt)) {
                spawnEnemy(e);
            }
            continue;
        }
        updateEnemyAI(e, audio);
        updateTankTransform(e);
    }

    if (_bossActive) {
        updateEnemyAI(_boss, audio);
        updateTankTransform(_boss);
    } else if (_bossPending) {
        if (reached(_bossAlertUntil)) {
            if (trySpawnBoss(audio)) _bossPending = false;
        } else if (reached(_bossKlaxonAt)) {
            // Two-tone klaxon during the alert. playTone() is skipped while a
            // WAV plays, so beeps under the kill's explosion are lost but the
            // later ones get through.
            audio.playTone((_bossKlaxonBeat++ % 2) ? 660 : 880, 180);
            _bossKlaxonAt = millis() + BOSS_KLAXON_MS;
        }
    }
}

// Turn towards the player at the spec's capped rate, close in to the
// standoff distance, then telegraph and fire once lined up.
void TankFluxGame::updateEnemyAI(Enemy &e, AudioEngine &audio) {
    const TankSpec &spec = *e.spec;
    const bool boss = isBoss(e);

    float dx = _x - e.x, dz = _z - e.z;
    float dist = sqrtf(dx * dx + dz * dz);

    // Class 3 leads its target: aims where the player will be when a shell
    // covering `dist` arrives. The others aim straight at the player, so
    // steady strafing still beats them.
    float aimX = _x, aimZ = _z;
    if (!boss && e.tankClass == CLASS_3) {
        float framesToImpact = dist / ENEMY_SHELL_SPEED;
        aimX += _vx * framesToImpact;
        aimZ += _vz * framesToImpact;
    }

    // Turn and movement are per reference frame, so both scale with how long
    // this frame actually took (see updateFrameScale()).
    float turnLimit = spec.turnRate * _frameScale;
    float want = bearingTo(e.x, e.z, aimX, aimZ);
    float err  = angleDiff(want, e.headingDeg);
    e.headingDeg = wrapAngle(e.headingDeg + constrain(err, -turnLimit, turnLimit));

    if (dist > (float)spec.standoff) {
        float speed = enemySpeed() * spec.speedMult;
        float step = (inRiver(e.x, e.z) ? speed * RIVER_SPEED_MULT : speed) * _frameScale;
        float hr = radians(e.headingDeg);
        float nx = e.x + sinf(hr) * step;
        float nz = e.z + cosf(hr) * step;
        if (!_arena.blocked(nx, nz, spec.radius) && !blockedByTank(e, nx, nz, false)) {
            e.x = nx;
            e.z = nz;
        } else {
            // Scrape round whatever it hit instead of grinding against it.
            e.headingDeg = wrapAngle(e.headingDeg + ENEMY_SCRAPE_TURN_DEG * _frameScale);
        }
        const float limit = (float)(ARENA_HALF - spec.radius);
        e.x = constrain(e.x, -limit, limit);
        e.z = constrain(e.z, -limit, limit);
    }

    // Follow-up shots of a boss burst, along its current heading.
    if (e.burstShotsLeft > 0 && reached(e.nextBurstAt)) {
        fireEnemyShell(e, e.headingDeg);
        e.burstShotsLeft--;
        e.nextBurstAt = millis() + BOSS_BURST_GAP_MS;
    }

    if (e.fireAt != 0) {
        // Telegraph running: fire when it ends, wherever it's aimed by then.
        if (reached(e.fireAt)) {
            e.fireAt = 0;
            setBarrelHot(e, false);
            fireVolley(e, audio);
            e.nextFireAt = fireDelay();
        }
    } else if (e.burstShotsLeft == 0 && fabsf(err) < spec.aimTolerance &&
               dist < (float)ENEMY_FIRE_RANGE && reached(e.nextFireAt)) {
        // Commit to a shot: barrel glows and a warning tone plays, giving
        // the player FIRE_TELEGRAPH_MS to move.
        e.fireAt = millis() + FIRE_TELEGRAPH_MS;
        setBarrelHot(e, true);
        audio.playTone(boss ? 330 : 520, 40);
    }
}

// One shell from the muzzle along headingDeg. Returns false if the shared
// shell pool is full.
bool TankFluxGame::fireEnemyShell(Enemy &e, float headingDeg) {
    const float muzzle = e.spec->muzzle;
    for (auto &s : _enemyShells) {
        if (s.active) continue;
        float hr = radians(headingDeg);
        fireShell(s, e.x + sinf(hr) * muzzle, e.z + cosf(hr) * muzzle,
                  headingDeg, ENEMY_SHELL_SPEED);
        return true;
    }
    return false;
}

// Regular tanks fire one shell. The boss alternates a 3-shell spread (hard
// to dodge sideways, easy to back out of) with a 3-shot burst down one line
// (easy to sidestep, punishing to sit still in).
void TankFluxGame::fireVolley(Enemy &e, AudioEngine &audio) {
    bool fired;
    if (isBoss(e)) {
        if (e.volley++ % 2 == 0) {
            fired  = fireEnemyShell(e, e.headingDeg - BOSS_SPREAD_DEG);
            fired |= fireEnemyShell(e, e.headingDeg);
            fired |= fireEnemyShell(e, e.headingDeg + BOSS_SPREAD_DEG);
        } else {
            fired = fireEnemyShell(e, e.headingDeg);
            e.burstShotsLeft = BOSS_BURST_SHOTS - 1;
            e.nextBurstAt = millis() + BOSS_BURST_GAP_MS;
        }
    } else {
        fired = fireEnemyShell(e, e.headingDeg);
    }
    // playWAV() stops whatever is playing, so this can cut a just-started
    // explosion short. Accepted on this single-channel audio setup.
    if (fired) audio.playWAV("/audio/shot.wav");
}

// Places the tank's five parts from its position and heading. The barrel is
// a Y-axis cylinder tipped onto its side (rotX=90); with Jet's Rz*Ry*Rx
// order, rotY=heading then points it along (sin h, 0, cos h). It's pushed
// forward by half its length so it sticks out of the turret front. Tracks
// sit either side along the right vector (cos h, -sin h).
void TankFluxGame::updateTankTransform(Enemy &e) {
    const TankSpec &spec = *e.spec;
    const int32_t heading = (int32_t)e.headingDeg;
    e.hull->setPosition((int32_t)e.x, spec.hullY, (int32_t)e.z);
    e.hull->setRotation(0, heading, 0);
    e.turret->setPosition((int32_t)e.x, spec.turretY, (int32_t)e.z);
    e.turret->setRotation(0, heading, 0);

    float hr = radians(e.headingDeg);
    int32_t bx = (int32_t)(e.x + sinf(hr) * (float)(spec.barrelLen / 2));
    int32_t bz = (int32_t)(e.z + cosf(hr) * (float)(spec.barrelLen / 2));
    e.barrel->setPosition(bx, spec.turretY, bz);
    e.barrel->setRotation(90, heading, 0);

    int32_t rx = (int32_t)(cosf(hr) * (float)spec.trackOffset);
    int32_t rz = (int32_t)(-sinf(hr) * (float)spec.trackOffset);
    e.trackL->setPosition((int32_t)e.x - rx, spec.trackY, (int32_t)e.z - rz);
    e.trackL->setRotation(0, heading, 0);
    e.trackR->setPosition((int32_t)e.x + rx, spec.trackY, (int32_t)e.z + rz);
    e.trackR->setRotation(0, heading, 0);
}

}  // namespace tankflux
