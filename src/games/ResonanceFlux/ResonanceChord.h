#ifndef RESONANCE_CHORD_H
#define RESONANCE_CHORD_H

// Included from ResonanceFluxGame.h. The Chord: the boss every fifth wave
// (with the BOSSES option on). One big figure of three ratios drawn over
// one another, brightest to dimmest, sitting at the left or right edge
// (the top and bottom are too near the core). Match the brightest layer
// (its stop and its phase) and fire to strip it; the next brightest is
// then the one. Fire at a dimmer layer in resonance and the stripped ones
// come back, with a misfire's static. While it's up it sends ordinary
// signals in every few seconds. From wave 10 its layers drift slowly in
// phase; from wave 15 its middle layer slides between two ratios.

namespace resonance {

inline bool ResonanceFluxGame::isChordWave(int wave) const {
    return _opt.bosses && wave % CHORD_EVERY == 0;
}

// Three different ratios from the dial, random phases; the drift and the
// morph by wave.
inline void ResonanceFluxGame::setupChord() {
    Chord &c = _chord;
    c = Chord{};
    c.active = true;
    int pool[RATIO_COUNT], n = 0;
    for (int i = 0; i < _stopCount; ++i) pool[n++] = _stops[i];
    for (int i = 0; i < CHORD_LAYERS; ++i) {             // shuffle the first three in
        const int j = i + (int)random(0, n - i);
        const int t = pool[i]; pool[i] = pool[j]; pool[j] = t;
        c.ratio[i] = (uint8_t)pool[i];
        c.phase[i] = frand(0, TWO_PI_F);
        c.drift[i] = _wave >= CHORD_DRIFT_WAVE ? (random(0, 2) ? 1 : -1) * frand(0.6f, 1.0f) * CHORD_DRIFT_RATE : 0;
    }
    c.morph = _wave >= CHORD_MORPH_WAVE && n > CHORD_LAYERS;
    if (c.morph) {
        c.morphA = c.ratio[1];
        c.morphB = (uint8_t)pool[CHORD_LAYERS];          // a ratio no other layer has
        c.morphAt = _now + CHORD_MORPH_MS;
    }
    c.left = random(0, 2) != 0;
    c.startedAt = _now;
    c.sendAt = _now + (unsigned long)(CHORD_SEND_MS / paceSpeed());
}

inline float ResonanceFluxGame::chordX() const {
    return _chord.left ? CHORD_R + 3.0f : W - CHORD_R - 3.0f;
}

// Drift and morph, and its escorts: ordinary signals every few seconds,
// a couple at a time at most.
inline void ResonanceFluxGame::updateChord() {
    Chord &c = _chord;
    if (!c.active) return;
    const float slow = _now < _dampUntil ? DAMPEN_SLOW : 1.0f;
    for (int i = 0; i < CHORD_LAYERS; ++i) {
        c.phase[i] = fmodf(c.phase[i] + c.drift[i] * slow * _dt + TWO_PI_F, TWO_PI_F);
    }
    if (c.morph && (long)(_now - c.morphAt) >= 0) {
        c.ratio[1] = c.ratio[1] == c.morphA ? c.morphB : c.morphA;
        c.morphAt = _now + CHORD_MORPH_MS;
    }
    if ((long)(_now - c.sendAt) >= 0) {
        c.sendAt = _now + (unsigned long)(CHORD_SEND_MS / paceSpeed());
        if (aliveCount() < CHORD_ESCORTS_MAX) spawnSignal();
    }
}

// Of the layers still there, the one on your stop nearest your phase; in
// resonance inside the tolerance.
inline void ResonanceFluxGame::findChordMatch() {
    _chordFocus = _chordMatched = -1;
    _chordGap = 1e9f;
    if (!_chord.active) return;
    const int mine = _stops[_stop];
    const float p = phaseEff();
    for (int i = _chord.stripped; i < CHORD_LAYERS; ++i) {
        if (_chord.ratio[i] != mine) continue;
        const float g = phaseGap(RATIOS[mine], p, _chord.phase[i]);
        if (g < _chordGap) { _chordGap = g; _chordFocus = i; }
    }
    if (_chordFocus >= 0 && _chordGap < PHASE_TOL) _chordMatched = _chordFocus;
}

// A shot in resonance with a layer. The brightest: stripped (the last one
// brings the Chord down). A dimmer one: the stripped layers come back.
inline void ResonanceFluxGame::hitChord(int layer) {
    Chord &c = _chord;
    if (layer != c.stripped) {
        c.stripped = 0;
        ++_statMisfires;
        addStatic(STATIC_MISFIRE);
        _fireReadyAt = _now + FIRE_COOLDOWN_MS;
        _chordFocus = _chordMatched = -1;
        sfx(SFX_MISS);
        return;
    }
    chordShards(layer, false);
    _beamX = chordX();
    _beamY = CORE_Y;
    _beamUntil = _now + 160;
    _score += (long)(PTS_LAYER * scoreMult());
    ++c.stripped;
    ++_statLayers;
    _chordFocus = _chordMatched = -1;
    if (c.stripped < CHORD_LAYERS) {
        c.left = !c.left;                                // it jumps to the other side
        sfx(SFX_CHORD_HIT);
        return;
    }
    // Down: its escorts go with it, and the bonus for being quick.
    const float secs = (_now - c.startedAt) / 1000.0f;
    const float under = CHORD_PAR_S - secs;
    _score += (long)((PTS_CHORD + (under > 0 ? under * PTS_CHORD_SEC : 0)) * scoreMult());
    for (int i = 0; i < MAX_SIGNALS; ++i) if (_signals[i].alive) shatter(i);
    for (int i = 0; i < CHORD_LAYERS; ++i) chordShards(i, true);
    c.active = false;
    ++_statChords;
    sfx(SFX_CHORD_DOWN);
}

// A layer's figure flung apart (all of them, white, when it goes down).
inline void ResonanceFluxGame::chordShards(int layer, bool white) {
    const Ratio &r = RATIOS[_chord.ratio[layer]];
    const int n = segmentsFor(r, true) / 2;
    const float cx = chordX();
    int made = 0;
    for (auto &sh : _shards) {
        if (sh.until > _now) continue;
        float fx, fy;
        figurePoint(r, _chord.phase[layer], TWO_PI_F * made / n, fx, fy);
        sh.x = cx + fx * CHORD_R;
        sh.y = CORE_Y - fy * CHORD_R;
        sh.vx = fx * frand(25, 70) + frand(-8, 8);
        sh.vy = -fy * frand(25, 70) + frand(-8, 8);
        sh.until = _now + SHARD_MS + 200 - (unsigned long)random(0, 200);
        sh.white = white || (made & 3) == 0;
        if (++made >= n) break;
    }
}

}  // namespace resonance

#endif  // RESONANCE_CHORD_H
