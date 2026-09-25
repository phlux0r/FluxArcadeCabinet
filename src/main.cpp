// =============================================================================
// FLUX MASTER ARCADE — v2.0
// Main state machine orchestrator.
//
// To add a new game (see also src/games/IGame.h):
//   1. #include its header below
//   2. Instantiate it in the "Game instances" section
//   3. Add it to the gameRegistry[] array
//   4. Add a CabinetState for it in ArcadeConfig.h
//   5. Add a case to the switch in loop()
// =============================================================================

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <SD.h>

// Cabinet subsystems
#include "cabinet/ArcadeConfig.h"
#include "cabinet/InputManager.h"
#include "cabinet/AudioEngine.h"
#include "cabinet/ParticleManager.h"
#include "cabinet/PowerManager.h"

// Game interface
#include "games/IGame.h"

// Game implementations
#include "games/AsteroidFlux/AsteroidFluxGame.h"
#include "games/LanderFlux/LanderFluxGame.h"
#include "games/MazeFlux/MazeFluxGame.h"
#include "games/PlatformFlux/PlatformFluxGame.h"
#include "games/TankFlux/TankFluxGame.h"

// Launcher
#include "launcher/LauncherMenu.h"

// =============================================================================
// HARDWARE
// =============================================================================
Adafruit_ST7735 tft(ArcadeConfig::TFT_CS, ArcadeConfig::TFT_DC, ArcadeConfig::TFT_RST);

// Two canvases — one per physical orientation.
// We keep both allocated so switching games is instant (no heap allocation).
GFXcanvas16 canvasPortrait (ArcadeConfig::PORTRAIT_WIDTH,  ArcadeConfig::PORTRAIT_HEIGHT);
GFXcanvas16 canvasLandscape(ArcadeConfig::LANDSCAPE_WIDTH, ArcadeConfig::LANDSCAPE_HEIGHT);

// =============================================================================
// CABINET SUBSYSTEMS (shared across all games)
// =============================================================================
InputManager input;
AudioEngine  audio;
PowerManager powerMgr;

// =============================================================================
// GAME INSTANCES
// =============================================================================
AsteroidFluxGame asteroidGame;
LanderFluxGame   landerGame;
MazeFluxGame     mazeGame;
PlatformFluxGame platformGame;
TankFluxGame     tankGame;

// =============================================================================
// LAUNCHER
// =============================================================================
LauncherMenu launcher;

// Game registry — order determines menu order
const GameEntry gameRegistry[] = {
    { "Asteroids",  STATE_ASTEROID_FLUX },
    { "Lander",    STATE_LANDER_FLUX   },
    { "Maze", STATE_MAZE_FLUX },
    { "Runner",  STATE_PLATFORM_FLUX },
    { "Tank",  STATE_TANK_FLUX },
    // Add future games here: { "New Game", STATE_NEW_GAME },
};
const int GAME_COUNT = sizeof(gameRegistry) / sizeof(gameRegistry[0]);

// =============================================================================
// STATE
// =============================================================================
CabinetState cabinetState = STATE_LAUNCHER_MENU;
IGame*       activeGame   = nullptr;

// =============================================================================
// HELPERS
// =============================================================================

// Switch to a game: set rotation, resize canvas pointer, init game
void launchGame(IGame* game) {
    activeGame = game;
    uint8_t rotation = game->getRotation();
    tft.setRotation(rotation);
    input.waitForButtonARelease();  // Prevent launch-press bleeding into game
    game->init(audio);
    Serial.printf("[CABINET] Launched: %s (rotation %d)\n", game->getName(), rotation);
}

void returnToLauncher() {
    if (activeGame) activeGame->onExit();
    activeGame = nullptr;
    tft.setRotation(2);  // Portrait for menu
    launcher.onEnter(audio);
    cabinetState = STATE_LAUNCHER_MENU;
    Serial.println("[CABINET] Returned to launcher.");
}

// Return the correct canvas for the current display rotation
GFXcanvas16& activeCanvas() {
    return (tft.getRotation() == 1) ? canvasLandscape : canvasPortrait;
}

// Push the active canvas to the display
void flushCanvas() {
    GFXcanvas16& c = activeCanvas();
    tft.drawRGBBitmap(0, 0, c.getBuffer(), c.width(), c.height());
}

// =============================================================================
// SETUP
// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("[CABINET] Flux Master Arcade v2.0 starting...");

    // Backlight on
    pinMode(ArcadeConfig::TFT_BLK, OUTPUT);
    digitalWrite(ArcadeConfig::TFT_BLK, HIGH);

    // Onboard RGB LED holds its last colour indefinitely once powered —
    // drive it black so it doesn't sit lit through sleep or normal running.
    neopixelWrite(ArcadeConfig::RGB_LED_PIN, 0, 0, 0);

    // Display
    tft.initR(INITR_BLACKTAB);
    tft.setSPISpeed(ArcadeConfig::TFT_SPI_SPEED);
    SPI.setFrequency(ArcadeConfig::SPI_BUS_SPEED);
    tft.setRotation(2);  // Portrait for launcher menu
    tft.fillScreen(ArcadeConfig::COLOR_BLACK);

    // Input
    input.begin();
    powerMgr.begin();

    // Audio
    if (!audio.begin()) {
        Serial.println("[WARNING] Audio engine failed to start.");
    }

    // SD card (optional — cabinet runs without it)
    if (!SD.begin(ArcadeConfig::SD_CS)) {
        Serial.println("[WARNING] SD card not found — running without SD assets.");
    } else {
        Serial.println("[CABINET] SD card ready.");
    }

    // Seed RNG from floating analogue pins
    randomSeed(analogRead(0) + analogRead(ArcadeConfig::JOY_X) + micros());

    // Register games in the launcher
    launcher.setGames(gameRegistry, GAME_COUNT);
    launcher.onEnter(audio);

    audio.playLaunchMelody();

    Serial.println("[CABINET] Setup complete.");
}

// =============================================================================
// FRAME RATE INSTRUMENTATION — build with -DSHOW_FPS (see platformio.ini)
//
// loop() caps the frame rate, so "fps" sits at 60 whenever there's headroom
// and only drops once a frame overruns. The work figures are the real
// measurement: how long a frame's input, update and render actually took
// against that budget. Games move by a fixed amount per frame, so sustained
// work at or above the budget is what makes them play slow.
//
// Reported once a second, to the screen (top-left, over whatever the game
// drew) and to serial. Drawing the overlay itself costs a little, counted
// against the next frame rather than the one being reported.
// =============================================================================
#ifdef SHOW_FPS
namespace {
uint32_t fpsFrames = 0, fpsWorkSumUs = 0, fpsPeakUs = 0, fpsWindowStartUs = 0;
uint32_t fpsLastFps = 0, fpsLastAvgUs = 0, fpsLastPeakUs = 0;

void fpsAccumulate(uint32_t workUs) {
    fpsWorkSumUs += workUs;
    if (workUs > fpsPeakUs) fpsPeakUs = workUs;
    ++fpsFrames;

    if (micros() - fpsWindowStartUs < 1000000UL) return;
    fpsLastFps    = fpsFrames;
    fpsLastAvgUs  = fpsWorkSumUs / fpsFrames;
    fpsLastPeakUs = fpsPeakUs;
    fpsFrames = fpsWorkSumUs = fpsPeakUs = 0;
    fpsWindowStartUs = micros();
    Serial.printf("[FPS] %u fps | work avg %u.%02ums peak %u.%02ums | budget %u.%02ums\n",
                  (unsigned)fpsLastFps,
                  (unsigned)(fpsLastAvgUs / 1000), (unsigned)((fpsLastAvgUs % 1000) / 10),
                  (unsigned)(fpsLastPeakUs / 1000), (unsigned)((fpsLastPeakUs % 1000) / 10),
                  (unsigned)(ArcadeConfig::FRAME_INTERVAL_US / 1000),
                  (unsigned)((ArcadeConfig::FRAME_INTERVAL_US % 1000) / 10));
}

void fpsDraw() {
    char buf[24];
    snprintf(buf, sizeof(buf), "%u %u.%u/%u.%u", (unsigned)fpsLastFps,
             (unsigned)(fpsLastAvgUs / 1000), (unsigned)((fpsLastAvgUs % 1000) / 100),
             (unsigned)(fpsLastPeakUs / 1000), (unsigned)((fpsLastPeakUs % 1000) / 100));
    tft.setFont();
    tft.setTextSize(1);
    tft.setTextColor(ArcadeConfig::COLOR_WHITE, ArcadeConfig::COLOR_BLACK);
    tft.setCursor(0, 0);
    tft.print(buf);
}
}  // namespace
#endif

// =============================================================================
// MAIN LOOP
// =============================================================================
void loop() {
    // --- Frame timing ---
    static uint32_t lastFrameUs = 0;
    while (micros() - lastFrameUs < ArcadeConfig::FRAME_INTERVAL_US) {
        delayMicroseconds(10);
    }
    lastFrameUs = micros();

    // --- Input (read once, passed everywhere) ---
    input.update();
    const InputState& state = input.getState();

    // --- Audio (non-blocking update) ---
    audio.update();

    // --- State machine ---
    switch (cabinetState) {

        case STATE_LAUNCHER_MENU: {
            powerMgr.update(audio);  // power button only checked from the menu
            CabinetState next = launcher.update(canvasPortrait, state, audio);
            tft.drawRGBBitmap(0, 0, canvasPortrait.getBuffer(),
                              ArcadeConfig::PORTRAIT_WIDTH, ArcadeConfig::PORTRAIT_HEIGHT);

            if (next != STATE_LAUNCHER_MENU) {
                cabinetState = next;
                // Map state to game instance and launch
                switch (next) {
                    case STATE_ASTEROID_FLUX:
                        asteroidGame.setTFT(tft);
                        launchGame(&asteroidGame);
                        break;
                    case STATE_LANDER_FLUX:
                        landerGame.setTFT(tft);
                        launchGame(&landerGame);
                        break;
                    case STATE_MAZE_FLUX:
                        mazeGame.setTFT(tft);
                        launchGame(&mazeGame);
                        break;
                    case STATE_PLATFORM_FLUX:
                        platformGame.setTFT(tft);
                        launchGame(&platformGame);
                        break;
                    case STATE_TANK_FLUX:
                        launchGame(&tankGame);
                        break;
                    default: returnToLauncher(); break;
                }
            }
            break;
        }

        case STATE_ASTEROID_FLUX: {
            bool running = asteroidGame.update(canvasLandscape, state, audio);
            // Asteroid Flux flushes its own canvas internally (landscape)
            // because it did so in the standalone version — we keep that pattern.
            // If you prefer the flush here, remove the internal flush from the game.
            if (!running) returnToLauncher();
            break;
        }

        case STATE_LANDER_FLUX: {
            bool running = landerGame.update(canvasPortrait, state, audio);
            tft.drawRGBBitmap(0, 0, canvasPortrait.getBuffer(),
                              ArcadeConfig::PORTRAIT_WIDTH, ArcadeConfig::PORTRAIT_HEIGHT);
            if (!running) returnToLauncher();
            break;
        }

        case STATE_MAZE_FLUX: {
            bool running = mazeGame.update(canvasPortrait, state, audio);
            if (!running) returnToLauncher();
            break;
        }

        case STATE_PLATFORM_FLUX: {
            bool running = platformGame.update(canvasLandscape, state, audio);
            // Platform Flux flushes its own canvas internally (landscape),
            // same pattern as Asteroid Flux.
            if (!running) returnToLauncher();
            break;
        }

        case STATE_TANK_FLUX: {
            bool running = tankGame.update(canvasLandscape, state, audio);
            tft.drawRGBBitmap(0, 0, canvasLandscape.getBuffer(),
                              ArcadeConfig::LANDSCAPE_WIDTH, ArcadeConfig::LANDSCAPE_HEIGHT);
            if (!running) returnToLauncher();
            break;
        }
        // Add future game cases here

        default:
            returnToLauncher();
            break;
    }

#ifdef SHOW_FPS
    fpsAccumulate(micros() - lastFrameUs);   // before the overlay's own cost
    fpsDraw();
#endif
}
