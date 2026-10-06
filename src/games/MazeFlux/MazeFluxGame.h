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
        audio.playLanderStartSound(); // shared game-select jingle, same as Lander/Runner
    }

    bool update(GFXcanvas16 &canvas,
                const InputState &input,
                AudioEngine &audio) override {
        return _engine.update(canvas, audio, input);
    }

    void onQuit(AudioEngine &audio) override { _engine.onQuit(audio); }

    uint8_t getRotation() const override { return 2; }
    const char* getName() const override { return "Maze Flux"; }
    void setTFT(Adafruit_ST7735 &tft) override { _engine.setTFT(tft); }
    bool flushesItself() const override { return true; }
};

#endif // MAZE_FLUX_GAME_H
