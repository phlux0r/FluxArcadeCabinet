#ifndef IGAME_H
#define IGAME_H

#include <Adafruit_GFX.h>
#include "cabinet/InputManager.h"
#include "cabinet/AudioEngine.h"

// =============================================================================
// IGAME — Pure virtual interface for all Flux Arcade games
//
// To add a new game:
//   1. Create a class that inherits from IGame
//   2. Implement the four pure virtual methods below (onExit() is optional)
//   3. Add a new CabinetState entry in ArcadeConfig.h
//   4. Register the game in src/main.cpp
//
// The launcher owns the canvas and passes it by reference each frame.
// Games must NOT create their own display or canvas objects.
// =============================================================================

class IGame {
public:
    virtual ~IGame() {}

    // Called once when the game is selected from the launcher menu.
    // Use this to reset all game state, load assets, play intro sound etc.
    // audio is guaranteed to be initialised and ready.
    virtual void init(AudioEngine &audio) = 0;

    // Called every frame while the game is active.
    // canvas is sized to match this game's declared rotation.
    // input contains the current normalised state of all controls.
    //
    // Returns:
    //   true  — game is still running, keep calling update()
    //   false — game has exited (player quit or navigated back)
    //           The launcher will return to the menu on false.
    virtual bool update(GFXcanvas16 &canvas,
                        const InputState &input,
                        AudioEngine &audio) = 0;

    // The TFT rotation this game requires.
    // 1 = landscape (160×128) — Asteroid Flux
    // 2 = portrait  (128×160) — Lander Flux, Launcher
    virtual uint8_t getRotation() const = 0;

    // Short display name shown in the launcher menu (max ~16 chars)
    virtual const char* getName() const = 0;

    // Called when the game hands control back to the launcher (after
    // update() returns false). Free large allocations here so the heap is
    // available to other games; init() runs again on the next launch.
    virtual void onExit() {}
};

#endif // IGAME_H
