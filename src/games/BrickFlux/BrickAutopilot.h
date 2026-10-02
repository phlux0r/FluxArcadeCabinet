#ifndef BRICK_AUTOPILOT_H
#define BRICK_AUTOPILOT_H

// =============================================================================
// BRICK FLUX — the attract demo's player. It produces the same InputState
// a player would, so the demo runs on the game's own rules.
//
// It predicts where the next ball down will reach the bat (walls and
// ceiling only, not bricks) and gets there; tilts the bat so the rebound
// heads for the lowest brick (the one the wall will bring to the line
// first), or a boss's core, and takes that target's colour; dodges bolts
// of the other colour, or swaps to absorb them when there's time; detours
// for capsules and sparks; and, with the meter full, holds A and lets go
// just before the ball arrives, aiming the smash up the fullest column (or
// at the core). Most of its smashes are Perfect, not all.
// =============================================================================

namespace brickflux {

// The tilt that sends a ball arriving along (dx, dy) at the bat's centre
// closest to the point (targetX, targetY), by trying each degree.
inline float BrickFluxGame::apTiltFor(float landX, float dx, float dy, float targetX, float targetY) const {
    const float want = angleOf(targetX - landX, targetY - BAT_Y);
    float best = 0, bestErr = 1e9f;
    for (int t = -(int)TILT_MAX_DEG; t <= (int)TILT_MAX_DEG; ++t) {
        float ox = dx, oy = dy;
        bounceOffBat(ox, oy, (float)t, 0.0f);
        const float err = fabsf(angleOf(ox, oy) - want);
        if (err < bestErr) { bestErr = err; best = (float)t; }
    }
    return best;
}

inline InputState BrickFluxGame::autopilot() {
    InputState in{};
    const unsigned long now = millis();
    if (_round != ROUND_PLAY) {
        _apPrevA = _apPrevB = false;
        _apHolding = false;
        return in;
    }

    // The ball that matters: the one that will reach the bat line soonest.
    const float lineY = BAT_Y - BALL_HALF;
    const Ball *threat = nullptr;
    float landX = _batX, landDx = 0, bestDist = 1e9f;
    for (const auto &b : _balls) {
        if (!b.active || b.held) continue;
        float x, d, odx;
        if (!predictCrossing(b.x, b.y, b.dx, b.dy, lineY, x, d, &odx)) continue;
        if (d < bestDist) { bestDist = d; landX = x; landDx = odx; threat = &b; }
    }
    const float speed = ballSpeed();
    const float ballTime = threat ? bestDist / speed : 1e9f;

    // Its target: the lowest breakable brick, the nearest of those to where
    // the ball lands, and that brick's colour. A smash instead goes up the
    // column with most in it.
    float tx = W / 2, ty = FIELD_T;
    uint8_t wantPol = _batPol;
    int lowestRow = -1, bestCol = -1, bestCount = -1;
    for (int r = ROWS - 1; r >= 0 && lowestRow < 0; --r)
        for (int c = 0; c < COLS; ++c)
            if (BrickBoard::breakable(_board.cell(r, c))) {
                const float cx = BrickBoard::cellX(c) + CELL_W * 0.5f;
                if (lowestRow < 0 || fabsf(cx - landX) < fabsf(tx - landX)) {
                    tx = cx; ty = _board.cellY(r) + CELL_H * 0.5f;
                    if (BrickBoard::coloured(_board.cell(r, c))) wantPol = _board.cell(r, c).pol;
                }
                lowestRow = r;
            }
    for (int c = 0; c < COLS; ++c) {
        int n = 0;
        for (int r = 0; r < ROWS; ++r) n += _board.solid(r, c);
        if (n > bestCount) { bestCount = n; bestCol = c; }
    }
    float smashX = BrickBoard::cellX(bestCol) + CELL_W * 0.5f, smashY = _board.top();

    // A boss: its core (the Hive's lowest bud first; the Flux Engine's wall,
    // then a portal into its chamber), in the core's colour; the Warden's
    // in the colour of the shield brick lowest under it.
    if (_boss) {
        const Core *core = nullptr;
        for (const auto &c : _cores) if (c.active) { core = &c; break; }
        if (core) {
            smashX = core->x; smashY = core->y;
            if (_boss == BOSS_HIVE) {
                const Sat *low = nullptr;
                for (const auto &st : _sats) if (st.active && (!low || st.y > low->y)) low = &st;
                if (low) { tx = low->x + 3.5f; ty = low->y + 2; }
                else { tx = core->x; ty = core->y; }
            } else if (_boss == BOSS_ENGINE) {
                if (lowestRow < 0) {
                    int pr = -1, pc = -1;
                    for (int r = ROWS - 1; r >= 0 && pr < 0; --r)
                        for (int c = 0; c < COLS; ++c)
                            if (_board.cell(r, c).kind == BrickBoard::PORTAL) { pr = r; pc = c; break; }
                    if (pr >= 0) { tx = BrickBoard::cellX(pc) + CELL_W * 0.5f; ty = _board.cellY(pr) + CELL_H * 0.5f; }
                    else { tx = core->x; ty = core->y; }
                }
            } else {
                tx = core->x; ty = core->y;
                if (core->pol) wantPol = core->pol;
                const Sat *low = nullptr;
                for (const auto &st : _sats)
                    if (st.active && !st.gun && fabsf(st.x + 3.5f - core->x) < 14 && st.y > core->y && (!low || st.y > low->y)) low = &st;
                if (low && low->pol) wantPol = low->pol;
            }
        }
    }
    const bool smashing = _meter >= METER_FULL && threat && !threat->pierce && !anyHeld();

    float goalX = landX, tilt = 0;
    if (threat) {
        if (smashing) {
            tilt = constrain(angleOf(smashX - landX, smashY - BAT_Y) - 90.0f, -TILT_MAX_DEG, TILT_MAX_DEG);
        } else {
            tilt = apTiltFor(landX, landDx, fabsf(threat->dy), tx, ty);
        }
    }

    // A capsule or spark, if it gets there first with time to come back.
    auto detour = [&](float cx, float cy) {
        const float capTime = (BAT_Y - cy) / (cy < BAT_Y ? CAPSULE_SPEED : 1.0f);
        const float reach = fabsf(cx - _batX) / BAT_SPEED, back = fabsf(cx - landX) / BAT_SPEED;
        if (capTime > 0 && reach < capTime && capTime + back + 0.15f < ballTime) goalX = cx;
    };
    if (_capsule.active) detour(_capsule.x + CAPSULE_W * 0.5f, _capsule.y + CAPSULE_H);
    for (const auto &sp : _sparks) if (sp.active) detour(sp.x0, sp.y);

    // The nearest bolt coming down on the bat: one of the other colour is
    // absorbed (swapping to it) if the ball's not due first, else dodged.
    const Bolt *bolt = nullptr;
    float boltT = 1e9f, boltX = 0;
    for (const auto &bo : _bolts) {
        if (!bo.active || bo.vy <= 0) continue;
        const float t = (BAT_Y - bo.y) / bo.vy;
        const float xh = bo.x + bo.vx * t;
        if (t > 0 && t < 0.8f && fabsf(xh - goalX) < batW() * 0.5f + 4 && t < boltT) { bolt = &bo; boltT = t; boltX = xh; }
    }
    if (bolt && bolt->pol != _batPol) {
        if (ballTime > boltT + 0.3f) wantPol = bolt->pol;
        else if (ballTime > boltT - 0.1f) goalX = boltX + (landX > boltX ? 1.0f : -1.0f) * (batW() * 0.5f + 5);
    } else if (bolt && ballTime > boltT + 0.3f) {
        wantPol = _batPol;                       // already the bolt's colour: keep it
    }

    // Held balls: wait a moment, aim, then serve.
    bool tap = false, hold = false;
    if (anyHeld()) {
        if (!_apServeAt) _apServeAt = now + (unsigned long)random(300, 900);
        goalX = _batX;
        tilt = constrain(angleOf(tx - _batX, ty - BAT_Y) - 90.0f, -TILT_MAX_DEG, TILT_MAX_DEG);
        if (now >= _apServeAt) { tap = true; _apServeAt = 0; }
        _apHolding = false;
    } else if (smashing) {
        // Hold A early enough to charge; let go just before the ball lands.
        if (!_apHolding && ballTime > 0.35f) {
            _apHolding = true;
            _apReleaseMs = random(10) < 7 ? (float)random(10, 35) : (float)random(70, 130);
        }
        hold = _apHolding && !(_charging && ballTime * 1000.0f <= _apReleaseMs);
        if (!hold) _apHolding = false;
    } else {
        _apHolding = false;
        if (_effect == CAP_LASER && now >= _apLaserAt) { tap = true; _apLaserAt = now + 300; }
    }

    // The stick: across towards the goal, up/down for the tilt.
    const float err = goalX - _batX;
    in.joyX = fabsf(err) < 1.0f ? 0.0f : constrain(err / 10.0f, -1.0f, 1.0f);
    const float mag = fabsf(tilt) / TILT_MAX_DEG;
    in.joyY = mag < 0.02f ? 0.0f : (tilt > 0 ? 1.0f : -1.0f) * (TILT_DEADZONE + (1.0f - TILT_DEADZONE) * mag);

    // B: swap to the colour wanted, not mid-contact.
    const bool b = wantPol != _batPol && wantPol != POL_NONE && now - _swapAt >= SWAP_COOLDOWN_MS &&
                   ballTime > 0.12f && !_apPrevB;
    in.btnB = b;
    in.btnBPressed = b;
    _apPrevB = b;

    bool a = hold || (tap && !_apPrevA);
    in.btnA = a;
    in.btnAPressed = a && !_apPrevA;
    in.btnAReleased = !a && _apPrevA;
    _apPrevA = a;
    return in;
}

}  // namespace brickflux

#endif  // BRICK_AUTOPILOT_H
