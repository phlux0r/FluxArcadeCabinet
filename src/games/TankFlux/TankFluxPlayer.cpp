#include "TankFluxGame.h"

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

    // Strafe (hold B) and drive share _speed/SPEED_SMOOTH, so both ease in
    // and out the same way; strafe moves along the right vector (cos h, -sin h).
    float nx, nz;
    if (input.btnB) {
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
    _arena.pushOut(nx, nz, TANK_RADIUS);
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

void TankFluxGame::resolveEnemyCollision(float &x, float &z, AudioEngine &audio) {
    for (auto &e : _enemies) {
        if (e.alive) bumpTank(e, x, z, audio);
    }
    if (_bossActive) bumpTank(_boss, x, z, audio);
}

// Tanks are solid: push the player out, and charge BUMP_DAMAGE once per
// contact rather than every frame spent leaning on the tank.
void TankFluxGame::bumpTank(Enemy &e, float &x, float &z, AudioEngine &audio) {
    if (pushOutOfCircle(x, z, e.x, e.z, (float)(TANK_RADIUS + e.spec->radius))) {
        if (!e.playerBumping) {
            e.playerBumping = true;
            _health -= BUMP_DAMAGE;
            _damageFlashUntil = millis() + BUMP_FLASH_MS;
            audio.playTone(180, 70);   // dull thud, distinct from a shell hit
        }
    } else {
        e.playerBumping = false;
    }
}

void TankFluxGame::tryFire(const InputState &input, AudioEngine &audio) {
    if (!input.btnAPressed) return;
    if (_playerShell.active) return;              // one shell in flight
    if (!reached(_reloadAt)) return;

    float hr = radians(_headingDeg);
    fireShell(_playerShell,
              _x + sinf(hr) * PLAYER_MUZZLE, _z + cosf(hr) * PLAYER_MUZZLE,
              _headingDeg, PLAYER_SHELL_SPEED);
    _reloadAt = millis() + PLAYER_RELOAD_MS;
    _muzzleFlashUntil = millis() + MUZZLE_FLASH_MS;
    audio.playWAV("/audio/shot.wav");
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

// Moves a shell one frame; returns false once it's gone (out of range, out
// of the arena, or into an obstacle, which is what makes obstacles cover).
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
    if (_arena.blocked(s.x, s.z, SHELL_OBSTACLE_RADIUS)) {
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
            if (within(_playerShell.x, _playerShell.z, e.x, e.z, e.spec->killRadius)) {
                hitEnemy(e, audio);
                killShell(_playerShell);
                hit = true;
                break;
            }
        }
        if (!hit && _bossActive &&
            within(_playerShell.x, _playerShell.z, _boss.x, _boss.z, _boss.spec->killRadius)) {
            // A shell travelling roughly the way the boss faces came from
            // behind: rear armour takes extra damage.
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
            _particles.emitSparks(Renderer::Vec3f{ s.x, (float)SHELL_Y, s.z },
                                  Renderer::Vec3f{ 0, 1, 0 }, 300.0f, 16);
            _health -= HIT_DAMAGE;
            _damageFlashUntil = millis() + HIT_FLASH_MS;
            killShell(s);
            playExplosion(audio);
        }
    }
}

void TankFluxGame::updateKits(AudioEngine &audio) {
    for (int i = 0; i < REPAIR_COUNT; ++i) {
        RepairKit &k = _kits[i];
        if (!k.active) {
            if (reached(k.respawnAt)) {
                k.active = true;
                k.obj->enabled = true;
            }
            continue;
        }
        k.obj->rotate(0, 3, 0);   // slow spin, so pickups read as pickups

        // Collected on contact even at full health: a kit that silently
        // refuses to be picked up reads as a bug.
        float dx = _x - (float)_arena.kits[i].x;
        float dz = _z - (float)_arena.kits[i].z;
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
