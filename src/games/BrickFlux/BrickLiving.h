#ifndef BRICK_LIVING_H
#define BRICK_LIVING_H

// =============================================================================
// BRICK FLUX — polarity and the living bricks: the colour swap, mismatched
// bounces, magnets, portals, guns and their bolts, and sparks. Included
// from BrickFluxGame.h.
// =============================================================================

namespace brickflux {

// B swaps the bat between cyan and magenta, on the press so it's instant.
// The ball takes the new colour the next time it touches the bat.
inline void BrickFluxGame::updateButtonB(const InputState &in) {
    const bool pressed = in.btnB && !_bHeld;
    _bHeld = in.btnB;
    if (!pressed || _now - _swapAt < SWAP_COOLDOWN_MS) return;
    _swapAt = _now;
    _batPol = _batPol == POL_CYAN ? POL_MAGENTA : POL_CYAN;
    ++_statSwaps;
    sfx(SFX_SWAP, _batPol == POL_CYAN ? 1200 : 700);
}

// A ball of the wrong colour off a coloured brick: no damage, the chain's
// broken, and the brick flashes so it's clear why.
inline void BrickFluxGame::mismatch(Ball &b, float x, float y) {
    _chain = 1;
    ++_statMismatches;
    sfx(SFX_CLANK);
    flashAt(x, y, CELL_W - 1, CELL_H - 1);
    afterBounce(b, false);
}

inline void BrickFluxGame::flashAt(float x, float y, int w, int h) {
    Flash *slot = &_flashes[0];
    for (auto &f : _flashes) {
        if (f.until <= _now) { slot = &f; break; }
        if (f.until < slot->until) slot = &f;
    }
    *slot = Flash{ x, y, (int8_t)w, (int8_t)h, _now + 150 };
}

// Magnets bend a passing ball towards them: a turn of up to
// MAGNET_TURN_DEG a second right at the magnet, fading to nothing at
// MAGNET_RADIUS. A smash goes straight past.
inline void BrickFluxGame::applyMagnets(Ball &b) {
    if (b.pierce) return;
    for (int r = 0; r < ROWS; ++r)
        for (int c = 0; c < COLS; ++c) {
            if (_board.cell(r, c).kind != BrickBoard::MAGNET) continue;
            const float mx = BrickBoard::cellX(c) + CELL_W * 0.5f, my = _board.cellY(r) + CELL_H * 0.5f;
            const float dx = mx - b.x, dy = my - b.y, d = sqrtf(dx * dx + dy * dy);
            if (d >= MAGNET_RADIUS || d < 1.0f) continue;
            const float turn = MAGNET_TURN_DEG * (1.0f - d / MAGNET_RADIUS) * _dt;
            const float a = angleOf(b.dx, b.dy);
            float diff = angleOf(dx, dy) - a;
            while (diff > 180.0f) diff -= 360.0f;
            while (diff < -180.0f) diff += 360.0f;
            dirFromAngle(a + constrain(diff, -turn, turn), b.dx, b.dy);
        }
    if (fabsf(b.dy) < MIN_DY) {
        b.dy = b.dy < 0 ? -MIN_DY : MIN_DY;
        b.dx = (b.dx < 0 ? -1.0f : 1.0f) * sqrtf(1.0f - MIN_DY * MIN_DY);
    }
}

// Into a portal and out of its partner, going the same way, from the
// partner's centre (always clear: the cell's taller and wider than the
// ball, wherever its neighbours are); it ignores portals for a moment
// after, so it doesn't go straight back through.
inline void BrickFluxGame::checkPortal(Ball &b) {
    if (_now < b.portalUntil) return;
    int r, c, pr, pc;
    if (!_board.portalAt(b.x, b.y, r, c) || !_board.partnerOf(r, c, pr, pc)) return;
    _particles.spawnExplosion(b.x, b.y, 0x780F, 6, 300);
    b.x = BrickBoard::cellX(pc) + CELL_W * 0.5f;
    b.y = _board.cellY(pr) + CELL_H * 0.5f;
    b.portalUntil = _now + PORTAL_COOLDOWN_MS;
    _particles.spawnExplosion(b.x, b.y, 0xF81F, 6, 300);
    ++_statTeleports;
    sfx(SFX_PORTAL);
}

// Each gun fires a bolt of its eye's colour every GUN_MIN_MS-GUN_MAX_MS
// (sooner each loop), but only with a clear line down: a buried gun stays
// quiet until it's dug out. Guns wait while a ball waits to be served.
inline void BrickFluxGame::updateGuns() {
    if (_serving) return;
    const int dms = (int)(_dt * 1000.0f + 0.5f);
    for (int r = 0; r < ROWS; ++r)
        for (int c = 0; c < COLS; ++c) {
            BrickBoard::Cell &k = _board.at(r, c);
            if (k.kind != BrickBoard::GUN) continue;
            k.timer -= dms;
            if (k.timer > 0) continue;
            k.timer = (int16_t)(random((long)GUN_MIN_MS, (long)GUN_MAX_MS) * 10 / (10 + 2 * loopIndex()));
            if (_board.cellY(r) + CELL_H < FIELD_T || !_board.clearBelow(r, c)) continue;
            fireBolt(BrickBoard::cellX(c) + CELL_W * 0.5f, _board.cellY(r) + CELL_H, 0, BOLT_SPEED, k.pol);
        }
}

inline void BrickFluxGame::fireBolt(float x, float y, float vx, float vy, uint8_t pol) {
    for (auto &b : _bolts) {
        if (b.active) continue;
        b = Bolt{ true, x, y, vx, vy, pol };
        sfx(SFX_BOLT);
        return;
    }
}

// Bolts fall through everything to the bat. One of the bat's colour is
// absorbed into the meter; one of the other colour stuns the bat.
inline void BrickFluxGame::updateBolts() {
    for (auto &b : _bolts) {
        if (!b.active) continue;
        b.x += b.vx * _dt;
        b.y += b.vy * _dt;
        if (batCatches(b.x - 1, b.x + 1, b.y - 2, b.y + 2)) {
            b.active = false;
            if (b.pol == _batPol) {
                ++_statAbsorbed;
                if (_meter < METER_FULL && (_meter += ABSORB_METER) >= METER_FULL) { _meter = METER_FULL; sfx(SFX_READY); }
                else sfx(SFX_ABSORB);
                _particles.spawnExplosion(b.x, BAT_Y, polColour(b.pol), 6, 250);
            } else {
                ++_statStunned;
                _stunUntil = _now + STUN_MS;
                _charging = false;
                sfx(SFX_ZAP);
                _particles.spawnExplosion(b.x, BAT_Y, polColour(b.pol), 10, 400);
            }
        } else if (b.y > H || b.x < 0 || b.x > W) {
            b.active = false;
        }
    }
}

inline void BrickFluxGame::dropSpark(float x, float y) {
    for (auto &s : _sparks) {
        if (s.active) continue;
        s = Spark{ true, x, y, _now };
        return;
    }
}

// Sparks wobble down; catch one for PTS_SPARK_CATCH, and the third caught
// in a level is an extra life (once a level).
inline void BrickFluxGame::updateSparks() {
    for (auto &s : _sparks) {
        if (!s.active) continue;
        s.y += SPARK_SPEED * _dt;
        const float x = s.x0 + 3.0f * sinf((_now - s.at) / 160.0f);
        if (batCatches(x - 2, x + 2, s.y - 2, s.y + 2)) {
            s.active = false;
            ++_statSparks;
            addScore(PTS_SPARK_CATCH);
            addPopup(x, s.y - 6, PTS_SPARK_CATCH, 1);
            sfx(SFX_SPARK);
            if (++_sparksCaught >= SPARKS_FOR_LIFE && !_sparkLife) {
                _sparkLife = true;
                if (_lives < MAX_LIVES) { ++_lives; sfx(SFX_EXTRA); }
            }
        } else if (s.y > H) {
            s.active = false;
        }
    }
}

}  // namespace brickflux

#endif  // BRICK_LIVING_H
