#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <Arduino.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include "ArcadeConfig.h"
#include "AudioEngine.h"

// =============================================================================
// POWER MANAGER — deep-sleep power button
//
// The LiPo stays wired to the board's B+/B- pads at all times so the onboard
// charge circuit keeps working even while the unit looks "off". Power
// off/on is emulated instead of switched: holding the button puts the
// ESP32 into deep sleep (a few µA draw) and the same button wakes it back
// up via ext0, which resets and re-runs setup() like a fresh boot.
//
// The MAX98357A's SD_MODE pin is driven LOW before sleep (hardware shutdown,
// µA-level draw) and HIGH again on wake. Its level is held through deep
// sleep with gpio_hold — without that, the pin's own pull-up would float it
// back high the moment the CPU stops driving it, re-enabling the amp during
// what's supposed to be "off".
//
// Wiring: POWER_BTN GPIO -> switch -> GND (internal pull-up, active LOW).
// AMP_SD_MODE GPIO -> MAX98357A SD_MODE, with a ~100k pull-up to VDD.
// Call update() only while the launcher menu is active so a hold mid-game
// can't power the unit off.
// =============================================================================
class PowerManager {
private:
    unsigned long _pressStartMs = 0;
    bool          _wasPressed   = false;

public:
    void begin() {
        pinMode(ArcadeConfig::POWER_BTN, INPUT_PULLUP);

        // Release any hold left over from the last time we went to sleep,
        // then re-enable the amp.
        gpio_hold_dis((gpio_num_t)ArcadeConfig::AMP_SD_MODE);
        gpio_deep_sleep_hold_dis();
        pinMode(ArcadeConfig::AMP_SD_MODE, OUTPUT);
        digitalWrite(ArcadeConfig::AMP_SD_MODE, HIGH);

        if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
            Serial.println("[POWER] Woke from deep sleep (power button).");
        }
    }

    // Call once per frame, only while the launcher menu is active.
    void update(AudioEngine &audio) {
        bool pressed = (digitalRead(ArcadeConfig::POWER_BTN) == LOW);

        if (pressed && !_wasPressed) _pressStartMs = millis();
        _wasPressed = pressed;

        if (pressed && (millis() - _pressStartMs >= ArcadeConfig::POWER_HOLD_MS)) {
            sleepNow(audio);
        }
    }

private:
    void sleepNow(AudioEngine &audio) {
        Serial.println("[POWER] Hold detected — going to sleep.");
        audio.stopAll();
        digitalWrite(ArcadeConfig::TFT_BLK, LOW);  // backlight off

        // Shut the amp down via SD_MODE and hold that level through deep
        // sleep so its own pull-up doesn't float it back high once the CPU
        // stops actively driving the pin.
        digitalWrite(ArcadeConfig::AMP_SD_MODE, LOW);
        gpio_hold_en((gpio_num_t)ArcadeConfig::AMP_SD_MODE);
        gpio_deep_sleep_hold_en();

        // Wait for release — ext0 wakeup is level-triggered, so sleeping
        // while still held would wake immediately.
        while (digitalRead(ArcadeConfig::POWER_BTN) == LOW) {
            delay(10);
        }
        delay(50);  // debounce

        esp_sleep_enable_ext0_wakeup((gpio_num_t)ArcadeConfig::POWER_BTN, 0);
        esp_deep_sleep_start();
        // Never returns — wake is a reset back into setup().
    }
};

#endif // POWER_MANAGER_H
