#include "TankFluxGame.h"
#include "../../assets/shared/SharedAssets.h"

namespace tankflux {

int32_t TankFluxGame::tankRadius(const Enemy &e) const {
    return (&e == &_boss) ? BOSS_RADIUS : ENEMY_RADIUS;
}

// Would putting `self` at (x,z) overlap tank `o`? For a move (not a
// spawn) it only counts if the move also brings them closer, so two
// tanks that already overlap can still drive apart instead of locking.
bool TankFluxGame::crowdsTank(const Enemy &self, const Enemy &o, float x, float z, bool spawning) const {
    if (!within(x, z, o.x, o.z, tankRadius(self) + tankRadius(o))) return false;
    if (spawning) return true;
    float nx = x - o.x, nz = z - o.z;
    float cx = self.x - o.x, cz = self.z - o.z;
    return nx * nx + nz * nz < cx * cx + cz * cz;
}

bool TankFluxGame::blockedByTank(const Enemy &self, float x, float z, bool spawning) const {
    for (const auto &o : _enemies) {
        if (&o != &self && o.alive && crowdsTank(self, o, x, z, spawning)) return true;
    }
    return _bossActive && &self != &_boss && crowdsTank(self, _boss, x, z, spawning);
}

// How many tanks may be on the field at once. Ramps 1 -> 2 -> 3 so the
// opening is survivable while you learn where cover is.
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
    return ENEMY_SPEED + (float)(_level - 1) * 0.9f;
}

unsigned long TankFluxGame::fireDelay() const {
    unsigned long cut = (unsigned long)(_level - 1) * 320;
    unsigned long lo = (ENEMY_FIRE_MIN_MS > cut + ENEMY_FIRE_FLOOR_MS)
                     ? ENEMY_FIRE_MIN_MS - cut : ENEMY_FIRE_FLOOR_MS;
    unsigned long hi = (ENEMY_FIRE_MAX_MS > cut + ENEMY_FIRE_FLOOR_MS)
                     ? ENEMY_FIRE_MAX_MS - cut : ENEMY_FIRE_FLOOR_MS + 800;
    return millis() + (unsigned long)random((long)lo, (long)hi);
}

// Class only enters the mix once _level already puts more than one
// tank on the field (see enemyCap()) — the opening stays exactly the
// single-class fight it always was.
TankFluxGame::EnemyClass TankFluxGame::pickEnemyClass() const {
    if (_level <= 2) return CLASS_1;
    if (_level <= 4) return (random(0, 2) == 0) ? CLASS_1 : CLASS_2;
    int r = random(0, 3);
    return r == 0 ? CLASS_1 : (r == 1 ? CLASS_2 : CLASS_3);
}

void TankFluxGame::setBarrelHot(Enemy &e, bool hot) {
    Renderer::Material* normal = (&e == &_boss) ? &_bossTurretMat : &_enemyBarrelMat;
    setObjectMaterial(e.barrel, hot ? &_barrelHotMat : normal);
}

// Cancels a telegraphed shot or unfinished burst — on death/spawn, so
// a tank never comes back with a glowing barrel or a queued shot.
void TankFluxGame::resetFireState(Enemy &e) {
    e.fireAt = 0;
    e.volley = 0;
    e.burstShotsLeft = 0;
    e.playerBumping = false;
    setBarrelHot(e, false);
}

void TankFluxGame::spawnEnemy(Enemy &e) {
    // Spawn out on the perimeter, and not right on top of the player.
    for (int attempt = 0; attempt < 12; ++attempt) {
        float ang = radians((float)random(0, 360));
        float r   = (float)(ARENA_HALF - 400);
        float ex  = sinf(ang) * r;
        float ez  = cosf(ang) * r;
        if (blockedFor(ex, ez, ENEMY_RADIUS)) continue;
        if (blockedByTank(e, ex, ez, true)) continue;
        if (within(ex, ez, _x, _z, 1600)) continue;
        e.x = ex;
        e.z = ez;
        e.headingDeg = bearingTo(ex, ez, _x, _z);
        e.alive = true;
        resetFireState(e);
        e.tankClass = pickEnemyClass();
        e.hp = e.maxHp = CLASS_HP[e.tankClass];
        setObjectMaterial(e.turret, e.tankClass == CLASS_1 ? &_enemyTurretMat
                                   : e.tankClass == CLASS_2 ? &_enemyTurretMatClass2
                                                             : &_enemyTurretMatClass3);
        e.hull->enabled = true;
        e.turret->enabled = true;
        e.barrel->enabled = true;
        e.trackL->enabled = true;
        e.trackR->enabled = true;
        e.nextFireAt = fireDelay();
        return;
    }
    // Every candidate was blocked or too close; try again next frame.
    e.respawnAt = millis() + 400;
}

// A hit that doesn't kill gets a small spark burst and a clang tone,
// distinct from the shot sound and the explosion a kill gets. A
// damage > 1 hit (boss rear armour) gets a higher clang and more
// sparks so the player learns that flanking paid off.
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
    bool isBoss = (&e == &_boss);
    _particles.emitSparks(Renderer::Vec3f{ e.x, 120.0f, e.z },
                          Renderer::Vec3f{ 0, 1, 0 }, 520.0f, isBoss ? 46 : 26);
    e.alive = false;
    resetFireState(e);
    e.hull->enabled = false;
    e.turret->enabled = false;
    e.barrel->enabled = false;
    e.trackL->enabled = false;
    e.trackR->enabled = false;
    // Shared explosion asset (SharedAssets.h) — same /audio/explosion.wav
    // AsteroidFlux and LanderFlux already use, with the same PROGMEM
    // fallback. Firing keeps its own short tone (tryFire()'s 950Hz
    // blip, enemy fire's 420Hz one): a shot igniting and a shell
    // detonating are different events and shouldn't sound the same.
    audio.playExplosionSound(explosion_data, sizeof(explosion_data));

    if (isBoss) {
        // Doesn't touch _kills/_level: the boss is a detour from the
        // regular escalation, not a step in it, and counting its own
        // death toward _kills risks it landing on another multiple of
        // BOSS_EVERY_KILLS and re-triggering itself immediately.
        _bossActive = false;
        long fightSecs = (long)((millis() - _bossSpawnedAt) / 1000UL);
        _bossBonus = BOSS_TIME_BONUS_MAX - fightSecs * BOSS_TIME_BONUS_PER_SEC;
        if (_bossBonus < 0) _bossBonus = 0;
        _bossBonusUntil = millis() + BOSS_BONUS_SHOW_MS;
        _score += BOSS_SCORE + _bossBonus;
        _bossesDefeated++;   // next boss spawns with BOSS_HP_STEP more HP
        audio.playTone(1900, 300);   // bigger fanfare than the regular level-up cue
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
        _bossKlaxonAt = millis();   // see updateEnemies()
    }
}

// Returns false (and leaves _bossPending set) if every perimeter
// candidate was blocked — updateEnemies() retries next frame, the
// same pattern spawnEnemy() uses for the regular pool.
bool TankFluxGame::trySpawnBoss(AudioEngine &audio) {
    // Always on the far side of the arena from the player, not just
    // "reject if too close": a pure random-angle-plus-reject approach
    // (what the regular spawnEnemy() perimeter search does) can still
    // land within a couple hundred units of the player by chance —
    // "scary when he's right behind you" from playtest, a boss should
    // be seen coming. Base angle is the player's own angle from the
    // arena centre plus 180°, with modest jitter so it isn't perfectly
    // predictable every time. This is also the only way to GUARANTEE
    // separation regardless of where the player is standing: a fixed
    // minimum-distance floor would be impossible to satisfy (and loop
    // forever) whenever the player is near the arena centre, since
    // every point on the spawn circle is then roughly the same
    // distance away.
    float playerAngle = atan2f(_x, _z);   // matches bearingTo()'s atan2(dx,dz) convention
    for (int attempt = 0; attempt < 12; ++attempt) {
        float jitter = radians((float)random(-BOSS_SPAWN_JITTER_DEG, BOSS_SPAWN_JITTER_DEG + 1));
        float ang = playerAngle + PI + jitter;
        float r   = (float)(ARENA_HALF - 500);
        float ex  = sinf(ang) * r;
        float ez  = cosf(ang) * r;
        if (blockedFor(ex, ez, BOSS_RADIUS)) continue;
        if (blockedByTank(_boss, ex, ez, true)) continue;
        if (within(ex, ez, _x, _z, BOSS_MIN_SPAWN_DIST)) continue;
        _boss.x = ex;
        _boss.z = ez;
        _boss.headingDeg = bearingTo(ex, ez, _x, _z);
        _boss.alive = true;
        resetFireState(_boss);
        _bossSpawnedAt = millis();
        _boss.hp = _boss.maxHp = BOSS_HP + BOSS_HP_STEP * _bossesDefeated;
        _boss.hull->enabled   = true;
        _boss.turret->enabled = true;
        _boss.barrel->enabled = true;
        _boss.trackL->enabled = true;
        _boss.trackR->enabled = true;
        _boss.nextFireAt = fireDelay();
        _bossActive = true;
        audio.playTone(300, 400);   // low arrival cue, distinct from the kill fanfare
        return true;
    }
    return false;
}

// Chase/aim/fire — identical for the regular pool and the boss, so
// both call this rather than duplicating it. Mesh placement is NOT
// handled here since the boss uses different offsets for its larger
// geometry; see updateEnemyTransform()/updateBossTransform().
void TankFluxGame::updateEnemyAI(Enemy &e, float speed, AudioEngine &audio) {
    // Boss gets its own (slower/further-back) tuning instead of the
    // regular pool's — see BOSS_TURN_RATE's own comment for why.
    bool isBoss = (&e == &_boss);
    float turnRate     = isBoss ? BOSS_TURN_RATE     : ENEMY_TURN_RATE;
    float standoff     = isBoss ? (float)BOSS_STANDOFF : (float)ENEMY_STANDOFF;
    float aimTolerance = isBoss ? BOSS_AIM_TOLERANCE : ENEMY_AIM_TOLERANCE;
    const int32_t radius = tankRadius(e);

    float dx = _x - e.x, dz = _z - e.z;
    float dist = sqrtf(dx * dx + dz * dz);

    // Class 3 leads its target: aims where the player will be when a
    // shell covering `dist` arrives. Everything else aims straight at
    // the player, so steady strafing still beats the easier tanks.
    float aimX = _x, aimZ = _z;
    if (!isBoss && e.tankClass == CLASS_3) {
        float framesToImpact = dist / ENEMY_SHELL_SPEED;
        aimX += _vx * framesToImpact;
        aimZ += _vz * framesToImpact;
    }

    float want = bearingTo(e.x, e.z, aimX, aimZ);
    float err  = angleDiff(want, e.headingDeg);
    e.headingDeg = wrapAngle(e.headingDeg +
                             constrain(err, -turnRate, turnRate));

    if (dist > standoff) {
        float step = inRiver(e.x, e.z) ? speed * RIVER_SPEED_MULT : speed;
        float hr = radians(e.headingDeg);
        float nx = e.x + sinf(hr) * step;
        float nz = e.z + cosf(hr) * step;
        if (!blockedFor(nx, nz, radius) && !blockedByTank(e, nx, nz, false)) {
            e.x = nx;
            e.z = nz;
        } else {
            // Scrape around whatever it walked into rather than
            // grinding against it forever.
            e.headingDeg = wrapAngle(e.headingDeg + 9.0f);
        }
        const float limit = (float)(ARENA_HALF - radius);
        e.x = constrain(e.x, -limit, limit);
        e.z = constrain(e.z, -limit, limit);
    }

    // Follow-up shots of a boss burst, along its current heading.
    if (e.burstShotsLeft > 0 && (long)(millis() - e.nextBurstAt) >= 0) {
        fireEnemyShell(e, e.headingDeg);
        e.burstShotsLeft--;
        e.nextBurstAt = millis() + BOSS_BURST_GAP_MS;
    }

    if (e.fireAt != 0) {
        // Telegraph running: fire when it ends, wherever it's now aimed.
        if ((long)(millis() - e.fireAt) >= 0) {
            e.fireAt = 0;
            setBarrelHot(e, false);
            fireVolley(e, audio);
            e.nextFireAt = fireDelay();
        }
    } else if (e.burstShotsLeft == 0 && fabsf(err) < aimTolerance &&
               dist < (float)ENEMY_FIRE_RANGE && (long)(millis() - e.nextFireAt) >= 0) {
        // Commit to a shot: barrel glows and a warning tone plays, so
        // the player gets FIRE_TELEGRAPH_MS to get out of the way.
        e.fireAt = millis() + FIRE_TELEGRAPH_MS;
        setBarrelHot(e, true);
        audio.playTone(isBoss ? 330 : 520, 40);
    }
}

// One shell from `e`'s muzzle along headingDeg. Returns false if the
// shared shell pool is full.
bool TankFluxGame::fireEnemyShell(Enemy &e, float headingDeg) {
    const float muzzle = (&e == &_boss) ? 360.0f : 220.0f;
    for (auto &s : _enemyShells) {
        if (s.active) continue;
        float hr = radians(headingDeg);
        fireShell(s, e.x + sinf(hr) * muzzle, e.z + cosf(hr) * muzzle,
                  headingDeg, ENEMY_SHELL_SPEED);
        return true;
    }
    return false;
}

// Regular tanks fire one shell. The boss alternates a 3-shell spread
// (hard to dodge sideways, easy to back out of) with a 3-shot burst
// down one line (easy to sidestep, punishing to sit still in).
void TankFluxGame::fireVolley(Enemy &e, AudioEngine &audio) {
    bool fired;
    if (&e == &_boss) {
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
    // playWAV() stops whatever's playing first, so this can cut a
    // just-started explosion.wav short — accepted on this
    // single-channel setup, same as the player's own shot.
    if (fired) audio.playWAV("/audio/shot.wav");
}

// createCylinder's local axis is Y; rotX=90 tips it onto its side, and
// Object's actual composition order (checked in Scene.cpp: M = Rz*Ry*Rx,
// applied to local vertices — NOT the Y-then-X order
// Camera::transformDirection uses for a different purpose, confirmed by
// hand rather than assumed from that comment) then makes rotY=heading
// aim the now-horizontal barrel down (sin(heading), 0, cos(heading)) —
// this game's own forward convention everywhere else.
void TankFluxGame::updateEnemyTransform(Enemy &e) {
    e.hull->setPosition((int32_t)e.x, 55, (int32_t)e.z);
    e.hull->setRotation(0, (int32_t)e.headingDeg, 0);
    e.turret->setPosition((int32_t)e.x, 155, (int32_t)e.z);
    e.turret->setRotation(0, (int32_t)e.headingDeg, 0);

    // Offset forward by half its length so it reads as bolted to the
    // turret's front rather than centred through it.
    float barrelHr = radians(e.headingDeg);
    int32_t bx = (int32_t)(e.x + sinf(barrelHr) * (float)(BARREL_LENGTH / 2));
    int32_t bz = (int32_t)(e.z + cosf(barrelHr) * (float)(BARREL_LENGTH / 2));
    e.barrel->setPosition(bx, 155, bz);
    e.barrel->setRotation(90, (int32_t)e.headingDeg, 0);

    // Track strips: offset sideways from the hull centre along the
    // vector perpendicular to heading (sin h, cos h) — (cos h, -sin h)
    // — at a lower Y so they read as a base the hull sits on.
    int32_t rx = (int32_t)(cosf(barrelHr) * (float)TRACK_OFFSET);
    int32_t rz = (int32_t)(-sinf(barrelHr) * (float)TRACK_OFFSET);
    e.trackL->setPosition((int32_t)e.x - rx, 30, (int32_t)e.z - rz);
    e.trackL->setRotation(0, (int32_t)e.headingDeg, 0);
    e.trackR->setPosition((int32_t)e.x + rx, 30, (int32_t)e.z + rz);
    e.trackR->setRotation(0, (int32_t)e.headingDeg, 0);
}

// Same placement logic as updateEnemyTransform(), at the boss's own
// 1.6x dimensions/offsets/Y-heights instead of the regular tank's.
void TankFluxGame::updateBossTransform() {
    Enemy &e = _boss;
    e.hull->setPosition((int32_t)e.x, BOSS_HULL_Y, (int32_t)e.z);
    e.hull->setRotation(0, (int32_t)e.headingDeg, 0);
    e.turret->setPosition((int32_t)e.x, BOSS_TURRET_Y, (int32_t)e.z);
    e.turret->setRotation(0, (int32_t)e.headingDeg, 0);

    float barrelHr = radians(e.headingDeg);
    int32_t bx = (int32_t)(e.x + sinf(barrelHr) * (float)(BOSS_BARREL_LEN / 2));
    int32_t bz = (int32_t)(e.z + cosf(barrelHr) * (float)(BOSS_BARREL_LEN / 2));
    e.barrel->setPosition(bx, BOSS_TURRET_Y, bz);
    e.barrel->setRotation(90, (int32_t)e.headingDeg, 0);

    int32_t rx = (int32_t)(cosf(barrelHr) * (float)BOSS_TRACK_OFFSET);
    int32_t rz = (int32_t)(-sinf(barrelHr) * (float)BOSS_TRACK_OFFSET);
    e.trackL->setPosition((int32_t)e.x - rx, BOSS_TRACK_Y, (int32_t)e.z - rz);
    e.trackL->setRotation(0, (int32_t)e.headingDeg, 0);
    e.trackR->setPosition((int32_t)e.x + rx, BOSS_TRACK_Y, (int32_t)e.z + rz);
    e.trackR->setRotation(0, (int32_t)e.headingDeg, 0);
}

void TankFluxGame::updateEnemies(AudioEngine &audio) {
    for (auto &e : _enemies) {
        if (!e.alive) {
            // Boss fights don't backfill the regular pool — the field
            // stays boss-only (plus any stragglers already alive when
            // it triggered) until it's dead.
            if (_bossActive) continue;
            // Hold the slot shut while at cap, pushing the timer along so
            // a kill always buys a breather rather than being replaced
            // the same instant.
            if (aliveEnemies() >= enemyCap()) {
                e.respawnAt = millis() + ENEMY_RESPAWN_MS;
            } else if ((long)(millis() - e.respawnAt) >= 0) {
                spawnEnemy(e);
            }
            continue;
        }
        updateEnemyAI(e, enemySpeed(), audio);
        updateEnemyTransform(e);
    }

    if (_bossActive) {
        updateEnemyAI(_boss, enemySpeed() * BOSS_SPEED_MULT, audio);
        updateBossTransform();
    } else if (_bossPending) {
        if ((long)(millis() - _bossAlertUntil) >= 0) {
            if (trySpawnBoss(audio)) _bossPending = false;
        } else if ((long)(millis() - _bossKlaxonAt) >= 0) {
            // Two-tone klaxon through the alert window. playTone() is
            // skipped while a WAV plays, so the beeps that land during
            // the triggering kill's explosion are lost but later ones
            // still get through.
            audio.playTone((_bossKlaxonBeat++ % 2) ? 660 : 880, 180);
            _bossKlaxonAt = millis() + 400;
        }
    }
}

}  // namespace tankflux
