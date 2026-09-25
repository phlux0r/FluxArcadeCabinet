#include "TankFluxGame.h"
#include "../../assets/shared/SharedAssets.h"

namespace tankflux {

void TankFluxGame::updateDriving(const InputState &input, AudioEngine &audio) {
    _headingDeg += TURN_SIGN * input.joyY * TURN_RATE;
    while (_headingDeg >= 360.0f) _headingDeg -= 360.0f;
    while (_headingDeg <    0.0f) _headingDeg += 360.0f;

    float headRad = radians(_headingDeg);
    float fx = sinf(headRad);
    float fz = cosf(headRad);
    const float terrainMult = inRiver(_x, _z) ? RIVER_SPEED_MULT : 1.0f;
    const float prevX = _x, prevZ = _z;

    float nx, nz;
    if (input.btnB) {
        // Strafe mode: joyX moves along the tank's right vector
        // (perpendicular to heading, same (cos h, -sin h) used for the
        // enemy track strips) instead of driving forward/back. Reuses
        // _speed/SPEED_SMOOTH so strafing eases in/out exactly like
        // normal driving does, just along a different axis.
        float strafeDrive = STRAFE_SIGN * input.joyX;
        float target = strafeDrive * STRAFE_SPEED * terrainMult;
        _speed += (target - _speed) * SPEED_SMOOTH;
        float rx = fz, rz = -fx;
        nx = _x + rx * _speed;
        nz = _z + rz * _speed;
    } else {
        float drive  = DRIVE_SIGN * input.joyX;
        float target = drive * (drive >= 0.0f ? FWD_SPEED : REV_SPEED) * terrainMult;
        _speed += (target - _speed) * SPEED_SMOOTH;
        nx = _x + fx * _speed;
        nz = _z + fz * _speed;
    }
    resolveObstacleCollision(nx, nz);
    resolveEnemyCollision(nx, nz, audio);
    _x = nx;
    _z = nz;

    const float limit = (float)(ARENA_HALF - TANK_RADIUS);
    _x = constrain(_x, -limit, limit);
    _z = constrain(_z, -limit, limit);
    _vx = _x - prevX;
    _vz = _z - prevZ;

    _camera.setPosition((int32_t)_x, EYE_HEIGHT, (int32_t)_z);
    _camera.setRotation(0, (int32_t)_headingDeg, 0);
}

// Same push-out as resolveObstacleCollision(), against enemy tanks and
// the boss — driving straight through them read as a free pass before
// this existed. Also charges BUMP_DAMAGE the first frame contact
// starts (e.playerBumping is the rising-edge guard, so leaning on a
// tank continuously doesn't drain health every single frame).
void TankFluxGame::resolveEnemyCollision(float &x, float &z, AudioEngine &audio) {
    for (auto &e : _enemies) {
        if (e.alive) bumpTank(e, ENEMY_RADIUS, x, z, audio);
    }
    if (_bossActive) bumpTank(_boss, BOSS_RADIUS, x, z, audio);
}

void TankFluxGame::bumpTank(Enemy &e, int32_t enemyRadius, float &x, float &z, AudioEngine &audio) {
    float dx = x - e.x, dz = z - e.z;
    float r  = (float)(TANK_RADIUS + enemyRadius);
    float d2 = dx * dx + dz * dz;
    if (d2 < r * r) {
        float d = sqrtf(d2);
        if (d < 0.0001f) { dx = r; dz = 0.0f; d = r; }
        float push = (r - d) / d;
        x += dx * push;
        z += dz * push;
        if (!e.playerBumping) {
            e.playerBumping = true;
            _health -= BUMP_DAMAGE;
            _damageFlashUntil = millis() + 120;
            audio.playTone(180, 70);   // dull collision thud, distinct from a shell hit
        }
    } else {
        e.playerBumping = false;
    }
}

void TankFluxGame::fireShell(Shell &s, float x, float z, float headingDeg, float speed) {
    float hr = radians(headingDeg);
    s.x = x;
    s.z = z;
    s.vx = sinf(hr) * speed;
    s.vz = cosf(hr) * speed;
    s.travelled = 0.0f;
    s.active = true;
    s.obj->enabled = true;
    s.obj->setPosition((int32_t)s.x, SHELL_Y, (int32_t)s.z);
}

void TankFluxGame::killShell(Shell &s) {
    s.active = false;
    s.obj->enabled = false;
}

// Advances a shell and returns true while it's still in flight. Shells
// die on obstacles, which is what turns cover into actual cover — no
// separate line-of-sight test is needed anywhere else.
bool TankFluxGame::advanceShell(Shell &s, int32_t range) {
    s.x += s.vx;
    s.z += s.vz;
    s.travelled += sqrtf(s.vx * s.vx + s.vz * s.vz);
    s.obj->setPosition((int32_t)s.x, SHELL_Y, (int32_t)s.z);

    if (s.travelled > (float)range ||
        fabsf(s.x) > (float)ARENA_HALF || fabsf(s.z) > (float)ARENA_HALF) {
        killShell(s);
        return false;
    }
    if (blockedFor(s.x, s.z, 20)) {
        _particles.emitSparks(Renderer::Vec3f{ s.x, (float)SHELL_Y, s.z },
                              Renderer::Vec3f{ 0, 1, 0 }, 260.0f, 8);
        killShell(s);
        return false;
    }
    return true;
}

void TankFluxGame::updateShells(AudioEngine &audio) {
    if (_playerShell.active && advanceShell(_playerShell, PLAYER_SHELL_RANGE)) {
        bool hit = false;
        for (auto &e : _enemies) {
            if (!e.alive) continue;
            if (within(_playerShell.x, _playerShell.z, e.x, e.z, KILL_RADIUS)) {
                hitEnemy(e, audio);
                killShell(_playerShell);
                hit = true;
                break;
            }
        }
        if (!hit && _bossActive &&
            within(_playerShell.x, _playerShell.z, _boss.x, _boss.z, BOSS_KILL_RADIUS)) {
            // Rear hit: the shell is travelling roughly the same way
            // the boss faces, i.e. it came from behind.
            float hr = radians(_boss.headingDeg);
            float sv = sqrtf(_playerShell.vx * _playerShell.vx + _playerShell.vz * _playerShell.vz);
            float along = (_playerShell.vx * sinf(hr) + _playerShell.vz * cosf(hr)) / sv;
            hitEnemy(_boss, audio, along > BOSS_REAR_ARC_COS ? BOSS_REAR_DAMAGE : 1);
            killShell(_playerShell);
        }
    }

    for (auto &s : _enemyShells) {
        if (!s.active) continue;
        if (!advanceShell(s, ENEMY_SHELL_RANGE)) continue;
        if (within(s.x, s.z, _x, _z, HIT_RADIUS)) {
            // Player-hit feedback: sparks and the same shared explosion
            // sound as destroyEnemy() right at the impact point,
            // complementing the existing screen flash. advanceShell()
            // already covers shell-vs-obstacle; this was the one impact
            // case with no particles or explosion audio at all.
            _particles.emitSparks(Renderer::Vec3f{ s.x, (float)SHELL_Y, s.z },
                                  Renderer::Vec3f{ 0, 1, 0 }, 300.0f, 16);
            _health -= HIT_DAMAGE;
            _damageFlashUntil = millis() + 160;
            killShell(s);
            audio.playExplosionSound(explosion_data, sizeof(explosion_data));
        }
    }
}

void TankFluxGame::tryFire(const InputState &input, AudioEngine &audio) {
    if (!input.btnAPressed) return;
    if (_playerShell.active) return;              // one shell in flight
    if ((long)(millis() - _reloadAt) < 0) return;

    float hr = radians(_headingDeg);
    fireShell(_playerShell,
              _x + sinf(hr) * 200.0f, _z + cosf(hr) * 200.0f,
              _headingDeg, PLAYER_SHELL_SPEED);
    _reloadAt = millis() + PLAYER_RELOAD_MS;
    _muzzleFlashUntil = millis() + 70;
    audio.playWAV("/audio/shot.wav");
}

void TankFluxGame::updateKits(AudioEngine &audio) {
    for (int i = 0; i < REPAIR_COUNT; ++i) {
        RepairKit &k = _kits[i];
        if (!k.active) {
            if ((long)(millis() - k.respawnAt) >= 0) {
                k.active = true;
                k.obj->enabled = true;
            }
            continue;
        }
        k.obj->rotate(0, 3, 0);   // slow spin, so pickups read as pickups

        // Picked up whenever you drive through, even at full health —
        // a kit that silently refuses to collect reads as a bug.
        float dx = _x - (float)REPAIRS[i].x;
        float dz = _z - (float)REPAIRS[i].z;
        if (dx * dx + dz * dz < (float)REPAIR_PICKUP_RADIUS * REPAIR_PICKUP_RADIUS) {
            _health = min(HEALTH_MAX, _health + REPAIR_AMOUNT);
            k.active = false;
            k.obj->enabled = false;
            k.respawnAt = millis() + REPAIR_RESPAWN_MS;
            audio.playWAV("/audio/repair.wav");
        }
    }
}

}  // namespace tankflux
