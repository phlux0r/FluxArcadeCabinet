#ifndef MAZE_FLUX_GAME_H
#define MAZE_FLUX_GAME_H

#include "../../games/IGame.h"
#include "../../cabinet/AudioEngine.h"
#include "GameEngineMaze.h"

class MazeFluxGame : public IGame {
private:
    GameEngineMaze _engine;

public:
    void init(AudioEngine &audio) override {
        _engine.init(audio);
    }

    bool update(GFXcanvas16 &canvas,
                const InputState &input,
                AudioEngine &audio) override {
        return _engine.update(
            canvas,
            input.btnA,
            input.btnB,
            input.joyUp,
            input.joyDown,
            input.joyLeft,
            input.joyRight,
            audio
        );
    }

    uint8_t getRotation() const override { return 2; }
    const char* getName() const override { return "Maze Flux"; }
    void setTFT(Adafruit_ST7735 &tft) { _engine.setTFT(tft); }
};

#endif // MAZE_FLUX_GAME_H
