#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <SD.h>
#include "ArcadeConfig.h"
#include "LauncherMenu.h"
#include "GameEngineLander.h"  // Import our flat-tab game module!

// Instantiate hardware graphics pipes
Adafruit_ST7735 tft = Adafruit_ST7735(ArcadeConfig::TFT_CS, ArcadeConfig::TFT_DC, ArcadeConfig::TFT_RST);
GFXcanvas16 canvas(ArcadeConfig::SCREEN_WIDTH, ArcadeConfig::SCREEN_HEIGHT);

// Subsystem instantiations
GameState currentCabinetState = STATE_LAUNCHER_MENU;
LauncherMenu launcher;
GameEngineLander landerGame; // Instantiate the Lander cartridge engine!

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("[SYSTEM] Starting Master Arcade Console Setup...");

    // Initialize Backlight
    pinMode(ArcadeConfig::TFT_BLK, OUTPUT);
    digitalWrite(ArcadeConfig::TFT_BLK, HIGH);

    // Initialize display hardware orientation
    tft.initR(INITR_BLACKTAB);
    tft.setRotation(2); // Right side up on breadboard layout
    
    // Configure inputs
    pinMode(ArcadeConfig::BUTTON_A, INPUT_PULLUP);
    pinMode(ArcadeConfig::BUTTON_B, INPUT_PULLUP);

    // Initialize Shared SPI SD Card Module
    if (!SD.begin(ArcadeConfig::SD_CS)) {
        Serial.println("[WARNING] Launcher running without SD card access.");
    } else {
        Serial.println("[SYSTEM] SD System tied into launcher pipeline.");
    }

    launcher.init();
    Serial.println("[SYSTEM] Setup complete.");
}

void loop() {
    // Capture live snapshots of our inputs globally at the start of every frame
    bool btnA = (digitalRead(ArcadeConfig::BUTTON_A) == LOW);
    bool btnB = (digitalRead(ArcadeConfig::BUTTON_B) == LOW);
    int joyX  = analogRead(ArcadeConfig::JOY_X);
    int joyY  = analogRead(ArcadeConfig::JOY_Y);

    // Dynamic Frame Router
    switch (currentCabinetState) {
        
        case STATE_LAUNCHER_MENU:
            // Capture if user launched a game from menu selection
            currentCabinetState = launcher.update(canvas);
            
            // If the user just launched Lander, call its setup profile once
            if (currentCabinetState == STATE_LANDER_FLUX) {
                landerGame.init();
            }
            break;

        case STATE_ASTEROID_FLUX:
            // Placeholder page until we port Asteroids next
            canvas.fillScreen(ArcadeConfig::COLOR_BLUE);
            canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
            canvas.setCursor(15, 60);
            canvas.print("ASTEROID RUNNING");
            canvas.setCursor(15, 80);
            canvas.print("Press BTN B to Exit");
            
            if (btnB) currentCabinetState = STATE_LAUNCHER_MENU;
            break;

        case STATE_LANDER_FLUX:
            // Run active frame execution loop inside the modular class.
            // If the game returns false, it means the player hit the back button.
            bool keepRunning = landerGame.update(canvas, btnA, btnB, joyX, joyY);
            
            if (!keepRunning) {
                currentCabinetState = STATE_LAUNCHER_MENU;
                delay(200); // Quick debounce to prevent accidental double-jumps
            }
            break;
    }

    // Refresh display matrix instantly by pushing compiled frame memory buffer
    tft.drawRGBBitmap(0, 0, canvas.getBuffer(), ArcadeConfig::SCREEN_WIDTH, ArcadeConfig::SCREEN_HEIGHT);

    delay(16); // Target ~60fps performance timing
}