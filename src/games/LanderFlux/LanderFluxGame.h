#ifndef LANDER_FLUX_GAME_H
#define LANDER_FLUX_GAME_H

#include "../../games/IGame.h"
#include "../../cabinet/ArcadeConfig.h"
#include "../../cabinet/AudioEngine.h"
#include "GameEngineLander.h"

class LanderFluxGame : public IGame {
private:
    GameEngineLander _engine;

public:
    void init(AudioEngine &audio) override {
        _engine.init(audio);
    }

    // tft must be set before first update() — called by FluxMasterArcade.ino on launch
    void setTFT(Adafruit_ST7735 &tft) { _engine.setTFT(tft); }

    bool update(GFXcanvas16 &canvas,
                const InputState &input,
                AudioEngine &audio) override {
        return _engine.update(canvas,
                              input.btnA,
                              input.btnB,
                              input.rawJoyX,
                              input.rawJoyY,
                              audio);
    }

    uint8_t getRotation() const override { return 2; }
    const char* getName() const override { return "Lander Flux"; }
};

#endif // LANDER_FLUX_GAME_H