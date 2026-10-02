#include "TankFluxGame.h"

// The attract demo: a real game at a random level, played silently by the
// autopilot below, like Tube Flux's. A starts a real game, B leaves; time's
// up or the autopilot's tank destroyed goes back to the title.

namespace tankflux {

// Turns towards the nearest tank (the boss if there is one), holds a middle
// distance and fires when lined up; strafes aside when a tank's barrel
// glows while it's pointed this way. The test harness's bot, plus dodging.
InputState TankFluxGame::demoPilot() {
    InputState in{};
    float bx = 0, bz = 0, bd = 1e30f;
    bool have = false;
    for (auto &e : _enemies) {
        if (!e.alive) continue;
        float d = (e.x - _x) * (e.x - _x) + (e.z - _z) * (e.z - _z);
        if (d < bd) { bd = d; bx = e.x; bz = e.z; have = true; }
    }
    if (_bossActive) { bx = _boss.x; bz = _boss.z; have = true; }

    bool fire = false;
    if (have) {
        float err = angleDiff(bearingTo(_x, _z, bx, bz), _headingDeg);
        in.joyY = TURN_SIGN * constrain(err / 8.0f, -1.0f, 1.0f);
        float dist = sqrtf((bx - _x) * (bx - _x) + (bz - _z) * (bz - _z));
        in.joyX = DRIVE_SIGN * (dist > 1400 ? 1.0f : (dist < 700 ? -0.7f : 0.0f));
        fire = fabsf(err) < 3.0f;
    }

    // A glowing barrel pointed this way: sidestep, alternating sides.
    auto aimingHere = [&](const Enemy &e) {
        return e.alive && e.fireAt != 0 &&
               fabsf(angleDiff(bearingTo(e.x, e.z, _x, _z), e.headingDeg)) < 12.0f;
    };
    bool threat = _bossActive && aimingHere(_boss);
    for (auto &e : _enemies) threat |= aimingHere(e);
    if (threat && reached(_demoStrafeUntil)) {
        _demoStrafeUntil = millis() + DEMO_DODGE_MS;
        _demoStrafeDir = -_demoStrafeDir;
    }
    if (!reached(_demoStrafeUntil)) {
        in.btnB = true;
        in.joyX = STRAFE_SIGN * _demoStrafeDir;
    }

    // Each fire is a fresh press, as a player's would be.
    in.btnA = fire && !_demoPrevA;
    in.btnAPressed = in.btnA;
    _demoPrevA = in.btnA;
    return in;
}

// A fresh game, fast-forwarded to a random level: the kills that level
// takes, and sometimes a boss due on the next kill.
void TankFluxGame::startDemo() {
    resetGame();
    const int level = (int)random(DEMO_MIN_LEVEL, DEMO_MAX_LEVEL + 1);
    _level = level;
    _kills = (level - 1) * KILLS_PER_LEVEL;
    _nextBossAt = random(0, 100) < DEMO_BOSS_PCT ? _kills + 1 : _kills + BOSS_EVERY_KILLS;
    _phase = PHASE_ATTRACT;
    _attractSlide = SLIDE_DEMO;
    _attractSlideTimer = millis();
    _demoUntil = millis() + (unsigned long)random((long)DEMO_MIN_MS, (long)DEMO_MAX_MS + 1);
    _demoStrafeUntil = 0;
    _demoPrevA = false;
}

bool TankFluxGame::updateDemo(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) {
    if (input.btnAPressed) { endDemo(); startNewGame(audio); return true; }

    InputState in = demoPilot();
    audio.setSilenced(true);
    updatePlaying(canvas, in, audio);
    audio.setSilenced(false);

    // updatePlaying() moves a destroyed tank to game over; for the demo
    // that's the end, as is running out of time.
    if (_phase == PHASE_GAMEOVER || reached(_demoUntil)) {
        endDemo();
        return true;
    }
    _phase = PHASE_ATTRACT;
    drawDemoOverlay(canvas);
    return true;
}

// Back to the title, leaving nothing of the demo behind.
void TankFluxGame::endDemo() {
    resetGame();
    _score = 0;
    _phase = PHASE_ATTRACT;
    _attractSlide = SLIDE_GAME;
    _attractSlideTimer = millis();
}

void TankFluxGame::drawDemoOverlay(GFXcanvas16 &canvas) {
    canvas.setFont();
    canvas.setTextSize(1);
    canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
    canvas.setCursor(4, canvas.height() - 10);
    canvas.print("DEMO");
    if ((millis() / 500) % 2 == 0) {
        canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
        canvas.setCursor(canvas.width() - 82, canvas.height() - 10);
        canvas.print("[BTN A] START");
    }
}

}  // namespace tankflux
