#ifndef RESONANCE_AUTOPILOT_H
#define RESONANCE_AUTOPILOT_H

// Included from ResonanceFluxGame.h. A player for the host harness (and a
// start on the attract demo's): it takes the signal nearest the core,
// steps the dial to its ratio the shorter way round, then turns the
// phase whichever way closes the gap, and fires in resonance. It dampens
// when a signal is about to get through unmatched. On a new target it
// pauses a moment first, as a person would (the demo looks played).

namespace resonance {

inline InputState ResonanceFluxGame::autopilot() {
    InputState in{};
    int target = -1;
    float best = 1e9f;
    for (int i = 0; i < MAX_SIGNALS; ++i) {
        const Signal &s = _signals[i];
        if (!s.alive) continue;
        const float dx = s.x - CORE_X, dy = s.y - CORE_Y, d2 = dx * dx + dy * dy;
        if (d2 < best) { best = d2; target = i; }
    }
    if (target != _apTarget) {
        _apTarget = target;
        _apReadyAt = _now + (unsigned long)random((long)AP_REACT_MIN_MS, (long)AP_REACT_MAX_MS + 1);
    }
    const bool ready = (long)(_now - _apReadyAt) >= 0;
    bool fire = false, damp = false;
    if (target >= 0 && _round == ROUND_PLAY && ready) {
        const Signal &s = _signals[target];
        const Ratio &r = RATIOS[s.ratio];
        const int stop = _stopOf[s.ratio];
        if (stop != _stop) {
            // Step the shorter way round, one flick at a time.
            const int right = (stop - _stop + _stopCount) % _stopCount;
            if (!_apPrevStep) in.joyY = right <= _stopCount - right ? 1.0f : -1.0f;
            _apPrevStep = !_apPrevStep;
        } else {
            _apPrevStep = false;
            const float p = phaseEff();
            const float g = phaseGap(r, p, s.phase);
            if (_matched == target) {
                fire = true;
            } else {
                const float up = phaseGap(r, p + 0.05f, s.phase), down = phaseGap(r, p - 0.05f, s.phase);
                float push = g * 2.5f;
                if (push < 0.3f) push = 0.3f;
                if (push > 1) push = 1;
                in.joyX = up < down ? -push : push;     // screen up turns the phase forward
            }
        }
    }
    if (target >= 0 && _round == ROUND_PLAY) {
        const float reach = CORE_R + SIG_R * 0.5f + 12.0f;
        damp = best < reach * reach && _matched != target && _now >= _dampUntil;
    }
    in.btnA = fire;
    in.btnAPressed = fire && !_apPrevA;
    in.btnB = damp;
    in.btnBPressed = damp && !_apPrevB;
    _apPrevA = fire;
    _apPrevB = damp;
    return in;
}

}  // namespace resonance

#endif  // RESONANCE_AUTOPILOT_H
