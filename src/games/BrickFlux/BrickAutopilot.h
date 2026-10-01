#ifndef BRICK_AUTOPILOT_H
#define BRICK_AUTOPILOT_H

// =============================================================================
// BRICK FLUX — the attract demo's player. It produces the same InputState
// a player would, so the demo runs on the game's own rules.
//
// It predicts where the next ball down will reach the bat (walls and
// ceiling only, not bricks) and gets there; tilts the bat so the rebound
// heads for the lowest brick (the one the wall will bring to the line
// first); detours for a capsule when there's time; and, with the meter
// full, holds A and lets go just before the ball arrives, aiming the smash
// up the fullest column. Most of its smashes are Perfect, not all.
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
        _apPrevA = false;
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
    // the ball lands. A smash instead goes up the column with most in it.
    float tx = W / 2, ty = FIELD_T;
    int lowestRow = -1, bestCol = -1, bestCount = -1;
    for (int r = ROWS - 1; r >= 0 && lowestRow < 0; --r)
        for (int c = 0; c < COLS; ++c)
            if (BrickBoard::breakable(_board.cell(r, c))) {
                const float cx = BrickBoard::cellX(c) + CELL_W * 0.5f;
                if (lowestRow < 0 || fabsf(cx - landX) < fabsf(tx - landX)) { tx = cx; ty = _board.cellY(r) + CELL_H * 0.5f; }
                lowestRow = r;
            }
    for (int c = 0; c < COLS; ++c) {
        int n = 0;
        for (int r = 0; r < ROWS; ++r) n += _board.solid(r, c);
        if (n > bestCount) { bestCount = n; bestCol = c; }
    }
    const bool smashing = _meter >= METER_FULL && threat && !threat->pierce && !anyHeld();

    float goalX = landX, tilt = 0;
    if (threat) {
        if (smashing) {
            const float colX = BrickBoard::cellX(bestCol) + CELL_W * 0.5f;
            tilt = constrain(angleOf(colX - landX, _board.top() - BAT_Y) - 90.0f, -TILT_MAX_DEG, TILT_MAX_DEG);
        } else {
            tilt = apTiltFor(landX, landDx, fabsf(threat->dy), tx, ty);
        }
    }

    // A capsule, if it gets there first with time to come back.
    if (_capsule.active) {
        const float cx = _capsule.x + CAPSULE_W * 0.5f;
        const float capTime = (BAT_Y - (_capsule.y + CAPSULE_H)) / CAPSULE_SPEED;
        const float reach = fabsf(cx - _batX) / BAT_SPEED, back = fabsf(cx - landX) / BAT_SPEED;
        if (capTime > 0 && reach < capTime && capTime + back + 0.15f < ballTime) goalX = cx;
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

    bool a = hold || (tap && !_apPrevA);
    in.btnA = a;
    in.btnAPressed = a && !_apPrevA;
    in.btnAReleased = !a && _apPrevA;
    _apPrevA = a;
    return in;
}

}  // namespace brickflux

#endif  // BRICK_AUTOPILOT_H
