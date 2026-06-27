#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include <Arduino.h>
#include "ArcadeConfig.h"

// =============================================================================
// INPUT MANAGER
// Reads the joystick and both buttons once per frame.
// Applies deadzone, smoothing, and edge detection.
// Games and the launcher receive a const InputState& — they never call
// analogRead() or digitalRead() themselves.
// =============================================================================

// Normalised joystick axis value after deadzone and smoothing.
// Range: -1.0 (full left/up) to +1.0 (full right/down), 0.0 = centre/deadzone.
struct InputState {
    // --- Joystick axes (normalised -1.0 to +1.0) ---
    float joyX;         // Horizontal: negative = left,  positive = right
    float joyY;         // Vertical:   negative = up,    positive = down

    // --- Raw 12-bit ADC values (0–4095) if a game needs them ---
    int rawJoyX;
    int rawJoyY;

    // --- Button current state (true = held down this frame) ---
    bool btnA;
    bool btnB;

    // --- Edge detection: true only on the frame the button was pressed ---
    bool btnAPressed;   // Rising edge (released → pressed)
    bool btnBPressed;

    // --- Edge detection: true only on the frame the button was released ---
    bool btnAReleased;  // Falling edge (pressed → released)
    bool btnBReleased;

    // --- Convenience: joystick direction as discrete booleans ---
    // These use ArcadeConfig::JOY_THRESHOLD so are suitable for menu navigation
    bool joyUp;
    bool joyDown;
    bool joyLeft;
    bool joyRight;
};

class InputManager {
private:
    // Previous button states for edge detection
    bool _prevBtnA = false;
    bool _prevBtnB = false;

    // Exponential moving average filter state for joystick smoothing
    float _filteredX = ArcadeConfig::JOY_CENTER;
    float _filteredY = ArcadeConfig::JOY_CENTER;

    // Smoothing factor: 0.0 = no new sample, 1.0 = no smoothing
    // 0.25 gives light smoothing while staying responsive
    static constexpr float SMOOTH = 0.25f;

    // Current output — updated every call to update()
    InputState _state;

    // Normalise a raw ADC value to -1.0..+1.0 with deadzone applied.
    float normalise(int raw) {
        int offset = raw - ArcadeConfig::JOY_CENTER;
        if (abs(offset) < ArcadeConfig::JOY_DEADZONE) return 0.0f;

        // Scale the live range (outside deadzone) to -1.0..+1.0
        float range = (float)(ArcadeConfig::JOY_CENTER - ArcadeConfig::JOY_DEADZONE);
        float norm = (float)(abs(offset) - ArcadeConfig::JOY_DEADZONE) / range;
        norm = constrain(norm, 0.0f, 1.0f);
        return (offset < 0) ? -norm : norm;
    }

public:
    InputManager() {
        memset(&_state, 0, sizeof(_state));
    }

    void begin() {
        pinMode(ArcadeConfig::BUTTON_A, INPUT_PULLUP);
        pinMode(ArcadeConfig::BUTTON_B, INPUT_PULLUP);
        // Analogue pins need no pinMode on ESP32
    }

    // Call once at the top of every loop() iteration, before any game update.
    void update() {
        // --- Joystick ---
        int rawX = analogRead(ArcadeConfig::JOY_X);
        int rawY = analogRead(ArcadeConfig::JOY_Y);

        // Apply exponential moving average
        _filteredX = (_filteredX * (1.0f - SMOOTH)) + (rawX * SMOOTH);
        _filteredY = (_filteredY * (1.0f - SMOOTH)) + (rawY * SMOOTH);

        _state.rawJoyX = rawX;
        _state.rawJoyY = rawY;
        _state.joyX = normalise((int)_filteredX);
        _state.joyY = normalise((int)_filteredY);

        // Discrete directional flags using raw threshold (snappier for menus)
        _state.joyUp    = (_filteredY < (ArcadeConfig::JOY_CENTER - ArcadeConfig::JOY_THRESHOLD));
        _state.joyDown  = (_filteredY > (ArcadeConfig::JOY_CENTER + ArcadeConfig::JOY_THRESHOLD));
        _state.joyLeft  = (_filteredX < (ArcadeConfig::JOY_CENTER - ArcadeConfig::JOY_THRESHOLD));
        _state.joyRight = (_filteredX > (ArcadeConfig::JOY_CENTER + ArcadeConfig::JOY_THRESHOLD));

        // --- Buttons (active LOW with INPUT_PULLUP) ---
        bool curA = (digitalRead(ArcadeConfig::BUTTON_A) == LOW);
        bool curB = (digitalRead(ArcadeConfig::BUTTON_B) == LOW);

        _state.btnA = curA;
        _state.btnB = curB;

        // Edge detection
        _state.btnAPressed  = (curA && !_prevBtnA);
        _state.btnAReleased = (!curA && _prevBtnA);
        _state.btnBPressed  = (curB && !_prevBtnB);
        _state.btnBReleased = (!curB && _prevBtnB);

        _prevBtnA = curA;
        _prevBtnB = curB;
    }

    // Immutable reference — games read this, never write to it
    const InputState& getState() const { return _state; }

    // Convenience: block until button A is released (useful after game launch
    // so the launch press doesn't immediately trigger in-game)
    void waitForButtonARelease() {
        while (digitalRead(ArcadeConfig::BUTTON_A) == LOW) { delay(10); }
        _prevBtnA = false;
    }

    void waitForButtonBRelease() {
        while (digitalRead(ArcadeConfig::BUTTON_B) == LOW) { delay(10); }
        _prevBtnB = false;
    }
};

#endif // INPUT_MANAGER_H
