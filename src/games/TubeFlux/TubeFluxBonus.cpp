#include "TubeFluxGame.h"

// The bonus round: see BONUS_* / PORTAL_* in TubeFluxConfig.h. Phases:
//   PORTAL  a stretch of one lane flashes gold ahead; fly through it
//   ROUND   30s of gem formations in a deep blue tunnel, nothing can hurt you
//   TALLY   the game pauses on the score, then play resumes where it was

namespace tubeflux {

namespace {

// Gem pool layout: slots are fixed to a type, like block widths.
inline int gemType(int slot) {
    return slot < BONUS_POOL_COMMON ? 0 : slot < BONUS_POOL_COMMON + BONUS_POOL_RARE ? 1 : 2;
}
const int GEM_POINTS[3] = { GEM_POINTS_COMMON, GEM_POINTS_RARE, GEM_POINTS_JACKPOT };

}  // namespace

void TubeFluxGame::updatePortal(AudioEngine &audio) {
    switch (_bonusPhase) {
        case BONUS_NONE:
            if (_tier >= MAX_TIER && armed() && !chasing() && _dist >= _nextPortalAt) {
                _bonusPhase = BONUS_PORTAL;
                _portalAt = _dist + SPAWN_AHEAD;
                _portalLane = _safeLane;          // never walled off
                setChaseBanner("PORTAL AHEAD!", ArcadeConfig::COLOR_YELLOW);
            }
            break;

        case BONUS_PORTAL: {
            const float shipAt = _dist + SHIP_Z;
            const bool inSpan = shipAt >= _portalAt - PORTAL_LENGTH * 0.5f &&
                                shipAt <= _portalAt + PORTAL_LENGTH * 0.5f;
            if (inSpan && fabsf(deltaDeg(_angle, (float)_portalLane * LANE_DEG)) < PORTAL_HALF_DEG) {
                startBonusRound(audio);
            } else if (shipAt > _portalAt + PORTAL_LENGTH * 0.5f) {
                // Missed: another comes along sooner than usual.
                _bonusPhase = BONUS_NONE;
                _nextPortalAt = _dist + PORTAL_EVERY * 0.5f;
                ++_portalsMissed;
            }
            break;
        }

        case BONUS_ROUND:
            updateBonusRound(audio);
            break;

        default:
            break;
    }
}

void TubeFluxGame::startBonusRound(AudioEngine &audio) {
    // Clear the tunnel: nothing from the main run comes with you.
    for (auto &o : _obstacles) { o.active = false; o.obj->enabled = false; }
    for (auto &c : _crystals)  { c.active = false; c.obj->enabled = false; }
    _pickupActive = false;
    _chevronObj->enabled = _crossObj->enabled = false;

    _bonusPhase = BONUS_ROUND;
    _bonusStartAt = millis();
    _gemsTotal = _gemsHit = 0;
    _bonusRoundPoints = 0;
    _bonusPerfect = false;
    _nextFormationAt = _dist + SPAWN_AHEAD * 0.5f;
    _bendTargetX = _bendTargetY = 0.0f;       // straightens out (see updateBend)
    ++_portalsEntered;
    applyTierPalette();
    _chaseBannerUntil = 0;
    static const int n[] = { 523, 784, 1047, 1568, 2093 };
    static const int d[] = {  50,  50,   50,   50,  200 };
    sfxMelody(audio, n, d, 5);
}

void TubeFluxGame::updateBonusRound(AudioEngine &audio) {
    const unsigned long elapsed = millis() - _bonusStartAt;
    if (elapsed < BONUS_SPAWN_STOP_MS) {
        while (_nextFormationAt < _dist + SPAWN_AHEAD) spawnFormation(_nextFormationAt);
    }
    updateGems();
    if (elapsed >= BONUS_ROUND_MS) startTally(audio);
}

// One formation at `at` (and a little beyond), then schedules the next.
// Mostly common gems; the odd rare one; now and then a jackpot.
void TubeFluxGame::spawnFormation(float at) {
    const int base = (int)random(0, TUBE_SIDES);
    const bool jackpot = random(0, 100) < 30;
    float span = 0.0f;
    // Every formation can be cleared by someone good: a full ring of all 8
    // lanes at one depth couldn't (it's in range ~2s, and rolling through
    // every lane shooting each takes longer), so the widest is a 5-lane
    // arc, the rarest: 1 in 8; spirals, lines and Vs 2 in 8 each; zigzags
    // 1 in 8.
    static const uint8_t PICK[8] = { 0, 1, 1, 2, 2, 3, 3, 4 };
    switch (PICK[random(0, 8)]) {
        case 0:   // arc: five neighbouring lanes at once, rare in the middle, maybe a jackpot at one end
            for (int i = 0; i < 5; ++i) {
                int type = (i == 2) ? 1 : (jackpot && i == 4) ? 2 : 0;
                placeGem(type, (base + i) % TUBE_SIDES, at);
            }
            break;
        case 1: {  // spiral: six gems winding round the tunnel
            int dir = random(0, 2) ? 1 : -1;
            for (int i = 0; i < 6; ++i) {
                int type = (i == 5) ? (jackpot ? 2 : 1) : 0;
                placeGem(type, (base + dir * i + TUBE_SIDES * 2) % TUBE_SIDES, at + i * 380.0f);
            }
            span = 5 * 380.0f;
            break;
        }
        case 2:   // line: four down one lane, the last one worth more
            for (int i = 0; i < 4; ++i) {
                placeGem(i == 3 ? (jackpot ? 2 : 1) : 0, base, at + i * 320.0f);
            }
            span = 3 * 320.0f;
            break;
        case 3:   // V: five lanes, the middle one first and rare
            for (int k = -2; k <= 2; ++k) {
                int type = (k == 0) ? 1 : (jackpot && k == 2) ? 2 : 0;
                placeGem(type, (base + k + TUBE_SIDES) % TUBE_SIDES, at + abs(k) * 300.0f);
            }
            span = 2 * 300.0f;
            break;
        default:  // zigzag: five gems stepping between two lanes
            for (int i = 0; i < 5; ++i) {
                int type = (i == 4) ? (jackpot ? 2 : 1) : 0;
                placeGem(type, (base + (i & 1)) % TUBE_SIDES, at + i * 330.0f);
            }
            span = 4 * 330.0f;
            break;
    }
    _nextFormationAt = at + span + BONUS_FORMATION_GAP;
}

// A gem of `type` from its part of the pool. Counts towards the total only
// if placed, so a full pool can't make "perfect" impossible.
bool TubeFluxGame::placeGem(int type, int lane, float at) {
    for (int i = 0; i < BONUS_POOL; ++i) {
        Obstacle &g = _gems[i];
        if (g.active || gemType(i) != type) continue;
        g.active = true;
        g.resolved = false;
        g.lane = lane;
        g.at = at;
        g.obj->enabled = true;
        placeObstacle(g);
        ++_gemsTotal;
        return true;
    }
    return false;
}

void TubeFluxGame::updateGems() {
    // The jackpot gems pulse gold/white.
    const bool white = (millis() / 150) & 1;
    _gemMat[2][0].color = white ? (uint16_t)0xFFFF : (uint16_t)((31 << 11) | (56 << 5) | 4);

    for (auto &g : _gems) {
        if (!g.active) continue;
        const float z = g.at - _dist;
        if (z < -(float)CRYSTAL_WIDTH) {
            g.active = false;
            g.obj->enabled = false;
            continue;
        }
        placeObstacle(g);
        if (g.resolved) continue;
        const float half = (CRYSTAL_WIDTH + SHIP_DEPTH) * 0.5f;
        if (fabsf(z - SHIP_Z) < half &&
            fabsf(deltaDeg(_angle, (float)g.lane * LANE_DEG)) < LANE_DEG * 0.5f + SHIP_HALF_DEG) {
            // Flown into: it breaks, harmlessly, for nothing.
            g.resolved = true;
            g.active = false;
            g.obj->enabled = false;
            float x, y;
            lanePoint((float)g.lane * LANE_DEG, TUBE_RADIUS * 0.8f, z, x, y);
            _particles.emitSparks(Renderer::Vec3f{ x, y, z }, Renderer::Vec3f{ 0, 0, -1 }, 200.0f, 6);
        } else if (z < SHIP_Z - half) {
            g.resolved = true;
        }
    }
}

void TubeFluxGame::hitGem(Obstacle &g, AudioEngine &audio) {
    g.active = false;
    g.obj->enabled = false;
    ++_gemsHit;
    _bonusRoundPoints += g.points;
    const float floorR = TUBE_RADIUS * cosf(radians(LANE_DEG / 2));
    float x, y, z = g.at - _dist;
    lanePoint((float)g.lane * LANE_DEG, floorR - CRYSTAL_HEIGHT * 0.5f, z, x, y);
    _particles.emitSparks(Renderer::Vec3f{ x, y, z }, Renderer::Vec3f{ 0, 0, -1 }, 420.0f, 16);
    // Higher pitch for a better gem. A tone, so a shot's WAV isn't cut off.
    sfxTone(audio, g.points >= GEM_POINTS_JACKPOT ? 1760 : g.points >= GEM_POINTS_RARE ? 1320 : 990, 40);
}

// Time's up: the game pauses on the score (stepRun() does nothing during
// TALLY), which is banked now so the score line jumps with it.
void TubeFluxGame::startTally(AudioEngine &audio) {
    _bonusPhase = BONUS_TALLY;
    _tallyAt = millis();
    _bonusPerfect = _gemsTotal > 0 && _gemsHit == _gemsTotal;
    _bonus += _bonusRoundPoints + (_bonusPerfect ? BONUS_PERFECT_POINTS : 0);
    if (_bonusPerfect) ++_bonusPerfects;
    for (auto &g : _gems) { g.active = false; g.obj->enabled = false; }
    if (_bonusPerfect) {
        static const int n[] = { 784, 988, 1175, 1568, 1175, 1568, 2093 };
        static const int d[] = { 100, 100,  100,  160,  100,  160,  400 };
        sfxMelody(audio, n, d, 7);
    } else {
        static const int n[] = { 659, 784, 988, 1319 };
        static const int d[] = { 100, 100, 100,  300 };
        sfxMelody(audio, n, d, 4);
    }
}

// Back to the main run where the portal was: blocks resume from the edge of
// the fog, curves come back, and a chase can't pounce straight away.
void TubeFluxGame::endBonus() {
    _bonusPhase = BONUS_NONE;
    _nextPortalAt = _dist + PORTAL_EVERY;
    _nextSpawnAt = _dist + SPAWN_AHEAD;
    _nextBendAt = _dist;
    if (_nextChaseAt < _dist + BONUS_RESUME_GAP) _nextChaseAt = _dist + BONUS_RESUME_GAP;
    applyTierPalette();
}

void TubeFluxGame::hideBonus() {
    _bonusPhase = BONUS_NONE;
    for (auto &g : _gems) { g.active = false; if (g.obj) g.obj->enabled = false; }
    applyTierPalette();
}

// During the round, the banner line is the round's own: gems hit so far,
// out of those offered so far, and seconds left.
void TubeFluxGame::drawBonusHud(GFXcanvas16 &canvas) {
    long left = ((long)BONUS_ROUND_MS - (long)(millis() - _bonusStartAt) + 999) / 1000;
    if (left < 0) left = 0;
    char buf[48];
    snprintf(buf, sizeof(buf), "BONUS %d/%d  %lds", _gemsHit, _gemsTotal, left);
    drawCentred(canvas, buf, 13, ArcadeConfig::COLOR_YELLOW);
}

void TubeFluxGame::drawTally(GFXcanvas16 &canvas) {
    const int16_t W = canvas.width();
    canvas.fillRect(14, 30, W - 28, 60, 0x0843);
    canvas.drawRect(14, 30, W - 28, 60, ArcadeConfig::COLOR_YELLOW);
    drawCentred(canvas, "BONUS ROUND", 35, ArcadeConfig::COLOR_YELLOW);
    char buf[40];
    snprintf(buf, sizeof(buf), "GEMS %d / %d", _gemsHit, _gemsTotal);
    drawCentred(canvas, buf, 49, ArcadeConfig::COLOR_WHITE);
    snprintf(buf, sizeof(buf), "+%ld", _bonusRoundPoints);
    drawCentred(canvas, buf, 60, ArcadeConfig::COLOR_CYAN);
    if (_bonusPerfect && ((millis() / 250) & 1)) {
        snprintf(buf, sizeof(buf), "PERFECT! +%d", BONUS_PERFECT_POINTS);
        drawCentred(canvas, buf, 74, ArcadeConfig::COLOR_MAGENTA);
    }
}

}  // namespace tubeflux
