#ifndef BRICK_BOSSES_H
#define BRICK_BOSSES_H

// =============================================================================
// BRICK FLUX — the bosses, one every fifth level. Included from
// BrickFluxGame.h.
//
// A boss is one or two cores (Core) and loose bricks (Sat): shields that
// orbit or ride a core, or the Hive's budded guns drifting down. They're
// off the grid, so the ball checks them separately (freeSolidAt()), the
// same way it checks a brick. A core takes 1 from a ball, SMASH_DAMAGE from
// a Good smash and PERFECT_DAMAGE from a Perfect, which stops at it. After
// a ball's hit a core shrugs off balls for CORE_IMMUNE_MS (they still
// bounce), so a ball caught between a core and its shields can't pinball
// it down in seconds.
//
//   1. The Warden: a core sliding along the top in a ring of 12 orbiting
//      bricks of both colours, which grows back a brick every
//      WARDEN_REGROW_MS; single aimed bolts, alternating colour.
//   2. The Hive: a fixed core that buds guns every HIVE_BUD_MS; they creep
//      down, firing, and one reaching the danger line costs a life.
//   3. The Twins: a cyan core and a magenta one, each shielded by bricks of
//      its colour; only a ball of a core's colour harms it or its shield.
//      They swap places every TWIN_SWAP_MS.
//   4. The Flux Engine: a core in a chamber above a band of steel, reached
//      through portals (or a smash through the steel), over its own
//      advancing wall; fans of three bolts, mixed colours below half health.
// =============================================================================

namespace brickflux {

inline int BrickFluxGame::bossHp(int base) const {
    return (int)(base * (1.0f + BOSS_HP_LOOP_STEP * loopIndex()) + 0.5f);
}

inline const char* BrickFluxGame::bossName() const {
    switch (_boss) {
        case BOSS_WARDEN: return "THE WARDEN";
        case BOSS_HIVE:   return "THE HIVE";
        case BOSS_TWINS:  return "THE TWINS";
        case BOSS_ENGINE: return "FLUX ENGINE";
        default:          return "";
    }
}

inline void BrickFluxGame::addSat(float x, float y, uint8_t pol, bool gun, int core, float angle, float ox, float oy) {
    for (auto &s : _sats) {
        if (s.active) continue;
        s = Sat{};
        s.active = true;
        s.x = x; s.y = y;
        s.pol = pol;
        s.gun = gun;
        s.hits = gun ? 2 : 1;
        s.core = (int8_t)core;
        s.angle = angle; s.ox = ox; s.oy = oy;
        s.timer = (int16_t)random(2000, 4000);
        return;
    }
}

inline void BrickFluxGame::startBoss(int kind) {
    _boss = (uint8_t)kind;
    for (auto &c : _cores) c = Core{};
    for (auto &s : _sats) s.active = false;
    _bossVolley = 0;
    _twinSwapping = false;
    if (!kind) return;
    sfx(SFX_BOSS_WARN);
    const unsigned long t0 = millis() + BOSS_INTRO_MS;
    Core &a = _cores[0];
    a.active = true;
    a.fireAt = t0 + 1500;
    switch (kind) {
        case BOSS_WARDEN:
            a.x = 64; a.y = 44; a.w = 20; a.h = 8; a.vx = 22;
            a.hp = a.maxHp = bossHp(WARDEN_HP);
            // The ring: slot i at 30i degrees round from the ring's turn
            // (kept in ox, so a regrown brick goes back in its own slot).
            _wardenTurn = 0;
            for (int i = 0; i < 12; ++i) addSat(0, 0, i % 2 ? POL_MAGENTA : POL_CYAN, false, 0, 0, (float)i, 0);
            _bossNextAt = t0 + WARDEN_REGROW_MS;
            break;
        case BOSS_HIVE: {
            a.x = 64; a.y = 30; a.w = 24; a.h = 10;
            a.hp = a.maxHp = bossHp(HIVE_HP);
            static const float XS[4] = { 18, 46, 76, 104 };
            for (float x : XS) addSat(x, 44, random(2) ? POL_CYAN : POL_MAGENTA, true, -1, 0, 0, 0);
            _bossNextAt = t0 + HIVE_BUD_MS;
            break;
        }
        case BOSS_TWINS: {
            Core &b = _cores[1];
            b.active = true;
            a.x = a.homeX = 36; b.x = b.homeX = 92;
            a.y = b.y = 36;
            a.w = b.w = 16; a.h = b.h = 8;
            a.pol = POL_CYAN; b.pol = POL_MAGENTA;
            a.hp = a.maxHp = b.hp = b.maxHp = bossHp(TWIN_HP);
            b.fireAt = t0 + 3000;
            // Six shield bricks each: one either side, four under.
            static const float OFF[6][2] = { { -16, -2 }, { 9, -2 }, { -16, 6 }, { -8, 6 }, { 0, 6 }, { 8, 6 } };
            for (int i = 0; i < 2; ++i)
                for (const auto &o : OFF) addSat(0, 0, _cores[i].pol, false, i, 0, o[0], o[1]);
            _twinSwapAt = t0 + TWIN_SWAP_MS;
            break;
        }
        case BOSS_ENGINE:
            a.x = 64; a.y = 22; a.w = 28; a.h = 10;
            a.hp = a.maxHp = bossHp(ENGINE_HP);
            break;
        default: break;
    }
    updateBoss();   // the shields into place for the intro
}

inline bool BrickFluxGame::bossBeaten() const {
    for (const auto &c : _cores) if (c.active) return false;
    return true;
}

// A Hive gun at the danger line: like the wall, a life, the ones that
// crossed go, and the rest are pushed back up.
inline void BrickFluxGame::hiveBreach() {
    for (auto &s : _sats) {
        if (!s.active) continue;
        if (s.y + CELL_H - 1 > DANGER_Y) {
            _particles.spawnExplosion(s.x + 3, s.y + 2, polColour(s.pol), 4, 600);
            s.active = false;
        } else {
            s.y = max(40.0f, s.y - WALL_PUSHBACK_ROWS * CELL_H);
        }
    }
    _shakeUntil = _now + SHAKE_MS * 2;
    ++_statWallLives;
    loseLife();
}

inline void BrickFluxGame::updateBoss() {
    if (!_boss) return;
    const unsigned long now = millis();
    const bool live = _round == ROUND_PLAY && !_serving;   // it only fights once the ball's in play
    const int dms = (int)(_dt * 1000.0f + 0.5f);
    const float fireScale = 1.0f / (1.0f + 0.2f * loopIndex());
    Core &a = _cores[0];

    switch (_boss) {
        case BOSS_WARDEN: {
            if (a.active) {
                a.x += a.vx * _dt;
                if (a.x < FIELD_L + 26) { a.x = FIELD_L + 26; a.vx = fabsf(a.vx); }
                if (a.x > FIELD_R - 26) { a.x = FIELD_R - 26; a.vx = -fabsf(a.vx); }
            }
            _wardenTurn += 40.0f * _dt;
            if (live && a.active && now >= _bossNextAt) {
                _bossNextAt = now + WARDEN_REGROW_MS;
                for (int slot = 0; slot < 12; ++slot) {
                    bool taken = false;
                    for (const auto &s : _sats) taken |= s.active && (int)s.ox == slot;
                    if (!taken) { addSat(0, 0, slot % 2 ? POL_MAGENTA : POL_CYAN, false, 0, 0, (float)slot, 0); break; }
                }
            }
            for (auto &s : _sats) {
                if (!s.active || s.core != 0) continue;
                s.angle = _wardenTurn + s.ox * 30.0f;
                s.x = a.x + 22.0f * cosf(degToRad(s.angle)) - 3.5f;
                s.y = a.y + 14.0f * sinf(degToRad(s.angle)) - 2.0f;
            }
            if (live && a.active && now >= a.fireAt) {
                a.fireAt = now + (unsigned long)(WARDEN_FIRE_MS * fireScale);
                const uint8_t pol = (_bossVolley++ % 2) ? POL_MAGENTA : POL_CYAN;
                fireBolt(a.x, a.y + a.h * 0.5f, constrain((_batX - a.x) * 0.25f, -30.0f, 30.0f), BOLT_SPEED, pol);
            }
            break;
        }
        case BOSS_HIVE: {
            if (live && a.active && now >= _bossNextAt) {
                _bossNextAt = now + (unsigned long)(HIVE_BUD_MS * fireScale);
                int buds = 0;
                for (const auto &s : _sats) buds += s.active;
                if (buds < HIVE_MAX_BUDS) {
                    // Somewhere along under the core not already taken.
                    for (int tries = 0; tries < 6; ++tries) {
                        const float x = (float)random(FIELD_L + 2, FIELD_R - 9), y = a.y + a.h * 0.5f + 4;
                        bool clear = true;
                        for (const auto &s : _sats)
                            if (s.active && fabsf(s.x - x) < 9 && fabsf(s.y - y) < 6) clear = false;
                        if (clear) { addSat(x, y, random(2) ? POL_CYAN : POL_MAGENTA, true, -1, 0, 0, 0); break; }
                    }
                }
            }
            if (!live) break;
            for (auto &s : _sats) {
                if (!s.active) continue;
                s.y += HIVE_DRIFT * _dt;
                s.timer -= dms;
                if (s.timer <= 0) {
                    s.timer = (int16_t)(random((long)GUN_MIN_MS, (long)GUN_MAX_MS) * fireScale);
                    bool covered = false;          // another bud right below takes the shot
                    for (const auto &o : _sats)
                        if (&o != &s && o.active && o.y > s.y && fabsf(o.x - s.x) < 7) covered = true;
                    if (!covered) fireBolt(s.x + 3.5f, s.y + 4, 0, BOLT_SPEED, s.pol);
                }
            }
            for (const auto &s : _sats)
                if (s.active && s.y + CELL_H - 1 > DANGER_Y) { hiveBreach(); return; }
            break;
        }
        case BOSS_TWINS: {
            Core &b = _cores[1];
            if (live && !_twinSwapping && now >= _twinSwapAt) {
                _twinSwapping = true;
                _bossNextAt = now;                 // when the swap began
                _twinFrom[0] = a.x; _twinFrom[1] = b.x;
                _twinTo[0] = b.active ? b.x : (a.x < 64 ? 92.0f : 36.0f);
                _twinTo[1] = a.active ? a.x : (b.x < 64 ? 92.0f : 36.0f);
            }
            if (_twinSwapping) {
                float t = (now - _bossNextAt) / 1000.0f;
                if (t >= 1.0f) { t = 1.0f; _twinSwapping = false; _twinSwapAt = now + TWIN_SWAP_MS; }
                const float e = t * t * (3 - 2 * t);
                for (int i = 0; i < 2; ++i) {
                    _cores[i].x = _twinFrom[i] + (_twinTo[i] - _twinFrom[i]) * e;
                    // Up and over each other rather than through.
                    _cores[i].y = 36 - (i ? -8.0f : 8.0f) * sinf(e * (float)PI);
                }
            }
            for (int i = 0; i < 2; ++i) {
                Core &c = _cores[i];
                if (live && c.active && now >= c.fireAt) {
                    c.fireAt = now + (unsigned long)(TWIN_FIRE_MS * fireScale);
                    fireBolt(c.x, c.y + c.h * 0.5f, constrain((_batX - c.x) * 0.2f, -25.0f, 25.0f), BOLT_SPEED, c.pol);
                }
            }
            for (auto &s : _sats)
                if (s.active && s.core >= 0) { s.x = _cores[s.core].x + s.ox; s.y = _cores[s.core].y + s.oy; }
            break;
        }
        case BOSS_ENGINE: {
            if (live && a.active && now >= a.fireAt) {
                a.fireAt = now + (unsigned long)(ENGINE_FIRE_MS * fireScale);
                const uint8_t pol = (_bossVolley++ % 2) ? POL_MAGENTA : POL_CYAN;
                const bool mixed = a.hp * 2 < a.maxHp;
                for (int k = -1; k <= 1; ++k) {
                    const uint8_t p = mixed && k == 0 ? (pol == POL_CYAN ? POL_MAGENTA : POL_CYAN) : pol;
                    fireBolt(a.x + k * 8, a.y + a.h * 0.5f, k * 25.0f, 55.0f, p);
                }
            }
            break;
        }
        default: break;
    }
}

// A core (kind 0) or loose brick (kind 1) a ball-sized box at (x, y) overlaps.
inline bool BrickFluxGame::freeSolidAt(float x, float y, int &kind, int &idx) const {
    const float x0 = x - BALL_HALF, x1 = x + BALL_HALF, y0 = y - BALL_HALF, y1 = y + BALL_HALF;
    for (int i = 0; i < 2; ++i) {
        const Core &c = _cores[i];
        if (!c.active) continue;
        if (x1 > c.x - c.w * 0.5f && x0 < c.x + c.w * 0.5f && y1 > c.y - c.h * 0.5f && y0 < c.y + c.h * 0.5f) {
            kind = 0; idx = i;
            return true;
        }
    }
    for (int i = 0; i < MAX_SATS; ++i) {
        const Sat &s = _sats[i];
        if (!s.active) continue;
        if (x1 > s.x && x0 < s.x + CELL_W - 1 && y1 > s.y && y0 < s.y + CELL_H - 1) {
            kind = 1; idx = i;
            return true;
        }
    }
    return false;
}

// A hit on a boss's core or loose brick, from a ball (b) or a laser (null).
// Polarity applies as to grid bricks: a shield or core of the other colour
// just turns the ball. A Hive gun is breakable by either colour.
inline void BrickFluxGame::hitFree(int kind, int idx, Ball *b, bool smashHit, bool perfect) {
    const uint8_t pol = b ? b->pol : POL_ANY;
    if (kind == 1) {
        Sat &s = _sats[idx];
        if (!s.gun && s.pol != POL_NONE && !smashHit && pol != POL_ANY && pol != s.pol) {
            if (b) mismatch(*b, s.x, s.y);
            return;
        }
        if (!smashHit && s.hits > 1) {
            --s.hits;
            addScore(PTS_CRACK);
            sfx(SFX_CRACK);
            if (b) b->idleBounces = 0;
            return;
        }
        s.active = false;
        int mult = smashHit ? (perfect ? 3 : 2) : 1;
        if (!s.gun && b && !smashHit) { mult = _chain; if (_chain < CHAIN_MAX) ++_chain; }
        brickBroken(s.x + 3.5f, s.y + 2, s.gun ? 0x8000 : polColour(s.pol), s.gun ? PTS_GUN : PTS_COLOURED, mult);
        if (b) b->idleBounces = 0;
        return;
    }
    Core &c = _cores[idx];
    if (c.pol != POL_NONE && !smashHit && pol != POL_ANY && pol != c.pol) {
        if (b) mismatch(*b, c.x - c.w * 0.5f, c.y - c.h * 0.5f);
        return;
    }
    if (!smashHit && _now < c.immuneUntil) { if (b) afterBounce(*b, false); return; }
    const int dmg = smashHit ? (perfect ? PERFECT_DAMAGE : SMASH_DAMAGE) : 1;
    if (!smashHit) c.immuneUntil = _now + CORE_IMMUNE_MS;
    c.hp -= dmg;
    c.flashUntil = _now + 120;
    addScore((long)PTS_CORE_HIT * dmg);
    _particles.spawnExplosion(c.x, c.y + c.h * 0.5f, ArcadeConfig::COLOR_WHITE, 4 + 2 * dmg, 350);
    sfx(SFX_BOSS_HIT);
    if (b) b->idleBounces = 0;
    if (c.hp <= 0) coreDown(idx);
}

// A core destroyed: a big blast, and its shields go with it. The last one
// takes every loose brick with it (and the level's then clear).
inline void BrickFluxGame::coreDown(int i) {
    Core &c = _cores[i];
    c.active = false;
    c.hp = 0;
    _shakeUntil = _now + SHAKE_MS * 3;
    _particles.spawnExplosion(c.x, c.y, ArcadeConfig::COLOR_WHITE, 30, 800);
    _particles.spawnExplosion(c.x, c.y, c.pol ? polColour(c.pol) : ArcadeConfig::COLOR_ORANGE, 30, 900);
    const bool last = bossBeaten();
    for (auto &s : _sats) {
        if (!s.active || (!last && s.core != i)) continue;
        _particles.spawnExplosion(s.x + 3, s.y + 2, polColour(s.pol), 3, 500);
        s.active = false;
    }
    if (last) for (auto &b : _bolts) b.active = false;
    sfx(SFX_BOSS_DIE);
}

// Cores: a dark body with a bright frame (its colour, or orange for a
// neutral core) and a pulsing eye; white for a moment when hit. Loose
// bricks as bricks: shields in their colour, Hive guns like grid guns.
inline void BrickFluxGame::drawBoss(GFXcanvas16 &cv, int ox, int oy) {
    if (!_boss) return;
    const unsigned long now = millis();
    for (const auto &c : _cores) {
        if (!c.active) continue;
        const int x = (int)(c.x - c.w * 0.5f) + ox, y = (int)(c.y - c.h * 0.5f) + oy, w = (int)c.w, h = (int)c.h;
        const bool hit = now < c.flashUntil;
        const uint16_t frame = hit ? 0xFFFF : c.pol ? polColour(c.pol) : ArcadeConfig::COLOR_ORANGE;
        cv.fillRect(x, y, w, h, hit ? 0xFFFF : (c.pol == POL_CYAN ? COL_CYAN_DIM : c.pol == POL_MAGENTA ? COL_MAGENTA_DIM : 0x3186));
        cv.drawRect(x, y, w, h, frame);
        const int eye = 1 + (int)((now / 150) % 3);
        cv.fillRect(x + w / 2 - eye, y + h / 2 - 1, eye * 2, 2, hit ? 0x0000 : ArcadeConfig::COLOR_RED);
    }
    for (const auto &s : _sats) {
        if (!s.active) continue;
        const int x = (int)s.x + ox, y = (int)s.y + oy;
        if (s.gun) {
            cv.drawRect(x, y, CELL_W - 1, CELL_H - 1, 0xF800);
            cv.fillRect(x + 1, y + 1, CELL_W - 3, CELL_H - 3, 0x8000);
            cv.fillRect(x + 2, y + 1, 3, 2, polColour(s.pol));
            if (s.hits < 2) cv.drawPixel(x + 5, y + 2, 0x0000);
        } else {
            cv.fillRect(x, y, CELL_W - 1, CELL_H - 1, polColour(s.pol));
            cv.drawPixel(x + 3, y + 1, 0x0000);
            cv.drawPixel(x + 3, y + 2, 0x0000);
        }
    }
}

}  // namespace brickflux

#endif  // BRICK_BOSSES_H
