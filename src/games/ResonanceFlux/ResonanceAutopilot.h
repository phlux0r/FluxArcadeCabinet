#ifndef RESONANCE_AUTOPILOT_H
#define RESONANCE_AUTOPILOT_H

// Included from ResonanceFluxGame.h. A player for the host harness (and a
// start on the attract demo's): it takes the signal nearest the core,
// steers the dial onto its ratio and lets the snap finish, then turns the
// phase whichever way closes the gap, and fires in resonance. It dampens
// when a signal is about to get through unmatched.

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
    bool fire = false, damp = false;
    if (target >= 0 && _round == ROUND_PLAY) {
        const Signal &s = _signals[target];
        const Ratio &r = RATIOS[s.ratio];
        const float stop = (float)_stopOf[s.ratio], gap = stop - _dial;
        if (fabsf(gap) > SNAP_ZONE * 0.5f) {
            float push = gap * 1.2f;
            if (fabsf(push) < 0.35f) push = push < 0 ? -0.35f : 0.35f;
            in.joyY = push > 1 ? 1 : push < -1 ? -1 : push;
        } else if (fabsf(dialEff() - stop) < RATIO_TOL) {
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
