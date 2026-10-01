#ifndef BRICK_PLAY_H
#define BRICK_PLAY_H

// =============================================================================
// BRICK FLUX — play: the bat, balls, bricks, the Flux Smash, capsules and
// the advancing wall. Included from BrickFluxGame.h.
// =============================================================================

namespace brickflux {

// The level's ball speed: +3% a level within a loop of the levels, each
// loop starting 10% quicker, capped; the Slow capsule takes a third off.
inline float BrickFluxGame::ballSpeed() const {
    const int inLoop = (_level - 1) % LEVELS_PER_LOOP;
    float s = BALL_SPEED0 * (1.0f + LOOP_SPEED_STEP * loopIndex()) * powf(1.0f + BALL_SPEED_STEP, (float)inLoop);
    if (s > BALL_SPEED_MAX) s = BALL_SPEED_MAX;
    if (_effect == CAP_SLOW) s *= SLOW_FACTOR;
    return s;
}

// The wall steps every 14s on level 1, half a second sooner each level to
// 7s, and each loop takes 15% more off, down to 4s.
inline unsigned long BrickFluxGame::wallStepMs() const {
    const int inLoop = (_level - 1) % LEVELS_PER_LOOP;
    long ms = (long)WALL_STEP_MS0 - (long)WALL_STEP_DEC * inLoop;
    if (ms < (long)WALL_STEP_MIN) ms = WALL_STEP_MIN;
    for (int i = 0; i < loopIndex(); ++i) ms = ms * 85 / 100;
    if (ms < (long)WALL_STEP_FLOOR) ms = WALL_STEP_FLOOR;
    return (unsigned long)ms;
}

inline int BrickFluxGame::activeBalls() const {
    int n = 0;
    for (const auto &b : _balls) n += b.active;
    return n;
}
inline bool BrickFluxGame::anyHeld() const {
    for (const auto &b : _balls) if (b.active && b.held) return true;
    return false;
}

// A level from the top: its layout, the formation back up, a ball on the
// bat, and the rows dropping in.
inline void BrickFluxGame::loadLevel() {
    _board.load(_level - 1);
    _capsule.active = false;
    for (auto &s : _shots) s.active = false;
    _effect = CAP_COUNT;
    _wallElapsed = 0;
    _warned = false;
    _lostThisLevel = false;
    _lowestReached = _board.lowestBottom();
    _round = ROUND_INTRO;
    _roundAt = millis();
    serveReset();
}

// One ball, on the bat, waiting for A. The wall waits too.
inline void BrickFluxGame::serveReset() {
    for (auto &b : _balls) b = Ball{};
    Ball &b = _balls[0];
    b.active = true;
    b.held = true;
    b.heldOffset = 0;
    b.x = _batX;
    b.y = BAT_Y - BALL_HALF - 0.5f;
    _serving = true;
    _charging = false;
    _releaseAt = 0;
}

inline void BrickFluxGame::stepPlay(const InputState &in) {
    _now = millis();
    _particles.update();
    for (auto &p : _popups) if (p.active && _now - p.at > POPUP_MS) p.active = false;
    updateBat(in);

    switch (_round) {
        case ROUND_INTRO:
            _aHeld = in.btnA;
            for (auto &b : _balls) if (b.active && b.held) b.x = _batX + b.heldOffset;
            if (_now - _roundAt >= INTRO_MS) { _round = ROUND_PLAY; _roundAt = _now; }
            return;
        case ROUND_LOST:
            _aHeld = in.btnA;
            if (_lives > 0 && _now - _roundAt >= LOST_MS) {
                serveReset();
                _round = ROUND_PLAY;
                _roundAt = _now;
            }
            return;
        case ROUND_CLEAR:
            _aHeld = in.btnA;
            if (_now - _roundAt >= CLEAR_MS) { ++_level; loadLevel(); }
            return;
        default:
            break;
    }

    updateButtonA(in);
    if (_charging && random(2)) {           // sparks off the charged bat's ends
        const float half = batW() * 0.5f;
        _particles.spawnFire(_batX + (random(2) ? half : -half), BAT_Y,
                             random(-10, 11) * 0.05f, -random(5, 15) * 0.1f, ArcadeConfig::COLOR_YELLOW);
    }
    if (_effect != CAP_COUNT && _now >= _effectUntil) endEffect();

    // Held balls ride the bat; a caught one goes by itself after a while.
    const float half = batW() * 0.5f;
    for (auto &b : _balls) {
        if (!b.active || !b.held) continue;
        b.heldOffset = constrain(b.heldOffset, -half + 1.0f, half - 1.0f);
        b.x = _batX + b.heldOffset;
        b.y = BAT_Y - BALL_HALF - 0.5f;
        if (!_serving && _now - b.heldSince >= CATCH_AUTO_RELEASE_MS) launchHeld(b);
    }

    const float dist = ballSpeed() * _dt;
    for (auto &b : _balls) if (b.active && !b.held) moveBall(b, dist);

    updateCapsules();
    updateShots();
    if (activeBalls() == 0) { loseLife(); return; }
    updateWall();
    if (_round != ROUND_PLAY) return;
    if (_board.remaining() == 0) levelCleared();
}

// Left/right moves the bat, its speed following the stick (gentle near the
// middle, for fine placing); up/down tilts it, past a deadzone so a sloppy
// sideways push doesn't. In portrait, screen up is +joyY.
inline void BrickFluxGame::updateBat(const InputState &in) {
    const float x = in.joyX;
    _batX += BAT_SPEED * x * (0.4f + 0.6f * fabsf(x)) * _dt;
    const float half = batW() * 0.5f;
    _batX = constrain(_batX, FIELD_L + half, FIELD_R - half);

    const float u = in.joyY;
    float mag = (fabsf(u) - TILT_DEADZONE) / (1.0f - TILT_DEADZONE);
    if (mag < 0) mag = 0;
    const float target = (u > 0 ? 1.0f : -1.0f) * mag * TILT_MAX_DEG;
    const float k = min(1.0f, _dt / TILT_EASE_S);
    _tilt += (target - _tilt) * k;
}

// A: a press serves (or lets a caught ball go), or fires the lasers. Held
// with the meter full, it charges the Flux Smash; the release opens the
// smash window (see batBounce()), and a release just after the ball left
// the bat still counts, as a Good.
inline void BrickFluxGame::updateButtonA(const InputState &in) {
    const bool pressed = in.btnA && !_aHeld, released = !in.btnA && _aHeld;
    _aHeld = in.btnA;
    if (pressed) {
        _aDownAt = _now;
        if (anyHeld()) releaseHeld();
        else if (_effect == CAP_LASER) fireLasers();
    }
    if (in.btnA && !_charging && _meter >= METER_FULL && !anyHeld() && _now - _aDownAt >= CHARGE_HOLD_MS)
        _charging = true;
    if (released && _charging) {
        _charging = false;
        for (auto &b : _balls) {
            if (b.active && !b.held && !b.pierce && b.contactAt && _now - b.contactAt <= LATE_RELEASE_MS) {
                smash(b, false);
                return;
            }
        }
        _releaseAt = _now;
    }
    if (_releaseAt && _now - _releaseAt > SMASH_WINDOW_MS) _releaseAt = 0;   // missed: the meter stays full
}

inline void BrickFluxGame::releaseHeld() {
    for (auto &b : _balls) if (b.active && b.held) launchHeld(b);
    if (_serving) sfx(SFX_SERVE);
    _serving = false;
}

// Off the bat from rest: where the bat aims, with English from where the
// ball sits on it. Never straight up, which would just bounce in place.
inline void BrickFluxGame::launchHeld(Ball &b) {
    const float off = constrain(b.heldOffset / (batW() * 0.5f), -1.0f, 1.0f);
    float a = batAimDeg(_tilt) - off * ENGLISH_DEG;
    if (fabsf(a - 90.0f) < 10.0f) a = off < 0 ? 105.0f : 75.0f;
    a = constrain(a, MIN_LAUNCH_DEG, 180.0f - MIN_LAUNCH_DEG);
    dirFromAngle(a, b.dx, b.dy);
    b.held = false;
    b.y = BAT_Y - BALL_HALF - 0.5f;
    b.contactAt = 0;
    b.idleBounces = 0;
}

// The ball moves in steps of at most SUBSTEP_PX, across then down, so it
// can't pass through a brick (4px) whatever its speed or the frame time.
// A brick in the way puts it back where it was on that axis and turns it.
inline void BrickFluxGame::moveBall(Ball &b, float dist) {
    const float lo = FIELD_L + BALL_HALF, hi = FIELD_R - BALL_HALF, top = FIELD_T + BALL_HALF;
    int n = (int)ceilf(dist / SUBSTEP_PX);
    if (n < 1) n = 1;
    const float step = dist / n;
    for (int i = 0; i < n; ++i) {
        if (!b.active || b.held) return;
        int r, c;
        // Across
        const float px = b.x;
        b.x += b.dx * step;
        if (b.x < lo) { b.x = 2 * lo - b.x; b.dx = fabsf(b.dx); afterBounce(b, false); }
        else if (b.x > hi) { b.x = 2 * hi - b.x; b.dx = -fabsf(b.dx); afterBounce(b, false); }
        if (b.pierce) pierceCells(b);
        else if (_board.solidAt(b.x, b.y, BALL_HALF, r, c)) {
            b.x = px;
            b.dx = -b.dx;
            hitCell(r, c, &b, false, 1);
        }
        // Down (or up)
        const float py = b.y, prevBottom = b.y + BALL_HALF;
        b.y += b.dy * step;
        if (b.y < top) {
            b.y = 2 * top - b.y;
            b.dy = fabsf(b.dy);
            b.pierce = b.perfect = false;       // a smash ends at the top wall
            afterBounce(b, false);
        }
        if (b.pierce) pierceCells(b);
        else if (_board.solidAt(b.x, b.y, BALL_HALF, r, c)) {
            b.y = py;
            b.dy = -b.dy;
            hitCell(r, c, &b, false, 1);
        }
        // The bat: the ball's bottom crossing its top, within its width.
        if (b.dy > 0 && prevBottom <= BAT_Y + 0.01f && b.y + BALL_HALF >= BAT_Y &&
            fabsf(b.x - _batX) <= batW() * 0.5f + BALL_HALF) {
            b.y = BAT_Y - BALL_HALF;
            batBounce(b);
        }
        if (b.y - BALL_HALF > H) { b.active = false; return; }
    }
}

// Off the bat. Inside the smash window it's a Flux Smash; otherwise the
// tilted bounce, and Catch holds it.
inline void BrickFluxGame::batBounce(Ball &b) {
    if (_releaseAt && _now - _releaseAt <= SMASH_WINDOW_MS) {
        const bool perfect = _now - _releaseAt <= PERFECT_MS;
        _releaseAt = 0;
        smash(b, perfect);
        return;
    }
    const float off = constrain((b.x - _batX) / (batW() * 0.5f), -1.0f, 1.0f);
    bounceOffBat(b.dx, b.dy, _tilt, off);
    b.idleBounces = 0;
    b.contactAt = _now;
    sfx(SFX_BAT);
    if (_effect == CAP_CATCH) {
        b.held = true;
        b.heldOffset = b.x - _batX;
        b.heldSince = _now;
    }
}

// The Flux Smash: straight out along the bat's aim, through everything to
// the top wall. A Perfect is three columns wide, scores more and shakes.
inline void BrickFluxGame::smash(Ball &b, bool perfect) {
    const float a = constrain(batAimDeg(_tilt), MIN_LAUNCH_DEG, 180.0f - MIN_LAUNCH_DEG);
    dirFromAngle(a, b.dx, b.dy);
    b.pierce = true;
    b.perfect = perfect;
    b.held = false;
    b.idleBounces = 0;
    b.contactAt = 0;
    if (b.y > BAT_Y - BALL_HALF) b.y = BAT_Y - BALL_HALF;
    _meter = 0;
    ++_statSmashes;
    if (perfect) {
        ++_statPerfects;
        _shakeUntil = _now + SHAKE_MS;
        _perfectFlashUntil = _now + 300;
        addScore(PERFECT_BONUS);
        addPopup(b.x, b.y - 8, PERFECT_BONUS, 1);
        _particles.spawnExplosion(b.x, b.y, ArcadeConfig::COLOR_WHITE, 30, 500);
        sfx(SFX_PERFECT);
    } else {
        _particles.spawnExplosion(b.x, b.y, ArcadeConfig::COLOR_YELLOW, 12, 400);
        sfx(SFX_SMASH);
    }
}

// A smashing ball takes every brick it touches; a Perfect one the columns
// either side too.
inline void BrickFluxGame::pierceCells(Ball &b) {
    const float ext = b.perfect ? (float)CELL_W : 0.0f;
    int r0, r1, c0, c1;
    if (!_board.span(b.x - BALL_HALF - ext, b.y - BALL_HALF, b.x + BALL_HALF + ext - 0.001f,
                     b.y + BALL_HALF - 0.001f, r0, r1, c0, c1)) return;
    for (int r = r0; r <= r1; ++r)
        for (int c = c0; c <= c1; ++c)
            if (_board.solid(r, c)) hitCell(r, c, &b, true, b.perfect ? 3 : 2);
}

inline void BrickFluxGame::hitCell(int r, int c, Ball *b, bool smashHit, int mult) {
    const float cx = BrickBoard::cellX(c) + CELL_W * 0.5f, cy = _board.cellY(r) + CELL_H * 0.5f;
    const BrickBoard::HitResult res = _board.hit(r, c, smashHit);
    if (!res.hit) return;
    if (res.broken) {
        brickBroken(cx, cy, res.colour, res.points, mult);
        if (b) b->idleBounces = 0;
    } else if (res.points > 0) {            // a hard brick cracked
        addScore((long)res.points * mult);
        _particles.spawnExplosion(cx, cy, res.colour, 2, 250);
        sfx(SFX_CRACK);
        if (b) b->idleBounces = 0;
    } else {                                // steel
        sfx(SFX_CLANK);
        if (b) afterBounce(*b, false);
    }
}

// After a bounce that broke nothing: a ball that's gone IDLE_BOUNCES of
// those in a row gets a small nudge, so it can't loop for ever between
// steel and the walls; and it never runs flatter than MIN_DY.
inline void BrickFluxGame::afterBounce(Ball &b, bool useful) {
    if (useful) b.idleBounces = 0;
    else if (++b.idleBounces >= IDLE_BOUNCES) {
        b.idleBounces = 0;
        float a = angleOf(b.dx, b.dy) + (random(2) ? 4.0f : -4.0f);
        dirFromAngle(a, b.dx, b.dy);
    }
    if (fabsf(b.dy) < MIN_DY) {
        b.dy = b.dy < 0 ? -MIN_DY : MIN_DY;
        b.dx = (b.dx < 0 ? -1.0f : 1.0f) * sqrtf(1.0f - MIN_DY * MIN_DY);
    }
}

inline void BrickFluxGame::brickBroken(float x, float y, uint16_t colour, int pts, int mult) {
    addScore((long)pts * mult);
    _particles.spawnExplosion(x, y, colour, 5, 400);
    sfx(SFX_BREAK);
    if (mult > 1) addPopup(x, y, pts * mult, (uint8_t)mult);
    if (_meter < METER_FULL && ++_meter >= METER_FULL) sfx(SFX_READY);
    if (!_capsule.active && random(CAPSULE_CHANCE) == 0) {
        _capsule.active = true;
        _capsule.kind = (uint8_t)random(CAP_COUNT);
        _capsule.x = constrain(x - CAPSULE_W * 0.5f, (float)FIELD_L, (float)(FIELD_R - CAPSULE_W));
        _capsule.y = y;
    }
}

// Extra lives at EXTRA_LIFE_FIRST and every EXTRA_LIFE_EVERY after, up to
// MAX_LIVES.
inline void BrickFluxGame::addScore(long pts) {
    _score += pts;
    while (_score >= _nextLifeAt) {
        _nextLifeAt += EXTRA_LIFE_EVERY;
        if (_lives < MAX_LIVES) { ++_lives; sfx(SFX_EXTRA); }
    }
}

inline void BrickFluxGame::addPopup(float x, float y, int32_t pts, uint8_t mult) {
    Popup *slot = &_popups[0];
    for (auto &p : _popups) {
        if (!p.active) { slot = &p; break; }
        if (p.at < slot->at) slot = &p;
    }
    *slot = Popup{ true, (int16_t)x, (int16_t)y, pts, mult, _now };
}

inline void BrickFluxGame::updateCapsules() {
    if (!_capsule.active) return;
    _capsule.y += CAPSULE_SPEED * _dt;
    const float half = batW() * 0.5f;
    if (_capsule.y + CAPSULE_H >= BAT_Y && _capsule.y <= BAT_Y + 3 &&
        _capsule.x + CAPSULE_W >= _batX - half && _capsule.x <= _batX + half) {
        _capsule.active = false;
        ++_statCapsules;
        sfx(SFX_CAPSULE);
        applyCapsule(_capsule.kind);
    } else if (_capsule.y > H) {
        _capsule.active = false;
    }
}

// Multi and Flux act at once; the rest are timed, one at a time, a new one
// replacing the last.
inline void BrickFluxGame::applyCapsule(uint8_t kind) {
    if (kind == CAP_FLUX) {
        if (_meter < METER_FULL) sfx(SFX_READY);
        _meter = METER_FULL;
        return;
    }
    if (kind == CAP_MULTI) {
        int originals[MAX_BALLS], n = 0;
        for (int i = 0; i < MAX_BALLS; ++i) if (_balls[i].active) originals[n++] = i;
        for (int k = 0; k < n; ++k) {
            Ball &src = _balls[originals[k]];
            if (src.held) { launchHeld(src); _serving = false; }
            for (int side = -1; side <= 1; side += 2) {
                Ball *slot = nullptr;
                for (auto &b : _balls) if (!b.active) { slot = &b; break; }
                if (!slot) return;
                *slot = src;
                slot->pierce = slot->perfect = false;
                dirFromAngle(angleOf(src.dx, src.dy) + side * 25.0f, slot->dx, slot->dy);
                afterBounce(*slot, true);
            }
        }
        return;
    }
    endEffect();
    _effect = kind;
    const unsigned long ms = kind == CAP_WIDE ? WIDE_MS : kind == CAP_CATCH ? CATCH_MS
                           : kind == CAP_LASER ? LASER_MS : SLOW_MS;
    _effectUntil = _now + ms;
}

// The timed effect runs out (or is replaced): a caught ball goes.
inline void BrickFluxGame::endEffect() {
    if (_effect == CAP_CATCH && !_serving)
        for (auto &b : _balls) if (b.active && b.held) launchHeld(b);
    _effect = CAP_COUNT;
}

inline void BrickFluxGame::fireLasers() {
    if (_now < _laserReadyAt) return;
    _laserReadyAt = _now + LASER_RELOAD_MS;
    const float half = batW() * 0.5f - 2.0f;
    int fired = 0;
    for (auto &s : _shots) {
        if (s.active) continue;
        s.active = true;
        s.x = _batX + (fired == 0 ? -half : half);
        s.y = BAT_Y - 2;
        if (++fired == 2) break;
    }
    sfx(SFX_LASER);
}

// Laser shots climb in 2px steps (a row is 5px), each taking one hit off
// the first brick it meets; steel stops them.
inline void BrickFluxGame::updateShots() {
    const float dist = SHOT_SPEED * _dt;
    const int n = (int)ceilf(dist / 2.0f);
    for (auto &s : _shots) {
        for (int i = 0; i < n && s.active; ++i) {
            s.y -= dist / n;
            int r, c;
            if (s.y < FIELD_T) s.active = false;
            else if (_board.solidAt(s.x, s.y, 0.5f, r, c)) {
                s.active = false;
                hitCell(r, c, nullptr, false, 1);
            }
        }
    }
}

// The advancing wall: a step down every wallStepMs() of play (not while a
// ball waits to be served), with a tick WALL_WARN_MS before. A breakable
// brick past the danger line costs a life and pushes the formation back up;
// steel there just shatters.
inline void BrickFluxGame::updateWall() {
    if (_serving) return;
    _wallElapsed += (unsigned long)(_dt * 1000.0f + 0.5f);
    const unsigned long step = wallStepMs();
    if (!_warned && _wallElapsed + WALL_WARN_MS >= step) { _warned = true; sfx(SFX_TICK); }
    if (_wallElapsed < step) return;
    _wallElapsed = 0;
    _warned = false;
    _board.stepDown();
    ++_statSteps;
    sfx(SFX_STEP);
    // A ball the formation came down onto goes out below it.
    for (auto &b : _balls) {
        if (!b.active || b.held) continue;
        int r, c;
        for (int guard = 0; guard < ROWS && _board.solidAt(b.x, b.y, BALL_HALF, r, c); ++guard) {
            b.y = _board.cellY(r) + CELL_H + BALL_HALF;
            b.dy = fabsf(b.dy);
        }
    }
    float xs[24], ys[24];
    uint16_t cols[24];
    if (_board.crossedDanger()) {
        const int n = _board.clearCrossed(xs, ys, cols, 24);
        for (int i = 0; i < n && i < 24; ++i) _particles.spawnExplosion(xs[i], ys[i], cols[i], 4, 600);
        _board.pushBack(WALL_PUSHBACK_ROWS);
        _shakeUntil = _now + SHAKE_MS * 2;
        ++_statWallLives;
        loseLife();
        return;
    }
    const int n = _board.shatterUnbreakable(xs, ys, 24);
    for (int i = 0; i < n && i < 24; ++i) _particles.spawnExplosion(xs[i], ys[i], COL_STEEL, 3, 400);
    const float lb = _board.lowestBottom();
    if (lb > _lowestReached) _lowestReached = lb;
}

// The last ball's gone, or the wall got through. The meter keeps half.
inline void BrickFluxGame::loseLife() {
    --_lives;
    _lostThisLevel = true;
    _meter /= 2;
    _charging = false;
    _releaseAt = 0;
    endEffect();
    _capsule.active = false;
    for (auto &s : _shots) s.active = false;
    for (auto &b : _balls) b.active = false;
    sfx(SFX_LOST);
    _round = ROUND_LOST;
    _roundAt = _now;
}

// Every breakable brick gone: the bonus for the level, the headroom left
// between the formation's lowest point and the danger line, and for not
// losing a life on it.
inline void BrickFluxGame::levelCleared() {
    _headroomRows = max(0, (int)((DANGER_Y - _lowestReached) / CELL_H));
    _clearBonus = CLEAR_BONUS + (long)_headroomRows * HEADROOM_PER_ROW + (_lostThisLevel ? 0 : NO_LOSS_BONUS);
    addScore(_clearBonus);
    for (auto &b : _balls) b.active = false;
    _capsule.active = false;
    for (auto &s : _shots) s.active = false;
    endEffect();
    _charging = false;
    _releaseAt = 0;
    ++_statCleared;
    sfx(SFX_CLEAR);
    _round = ROUND_CLEAR;
    _roundAt = _now;
}

}  // namespace brickflux

#endif  // BRICK_PLAY_H
