// =============================================================================
// FLUX MASTER ARCADE — v2.0
// Main state machine orchestrator.
//
// To add a new game (see also src/games/IGame.h):
//   1. #include its header below
//   2. Add it to the gameRegistry[] array: its menu name and makeGame<Type>
// Games are built when launched and destroyed on exit, so only the game
// being played takes RAM, however many there are; the menu scrolls.
// =============================================================================

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <SD.h>
#include <new>
#include <esp_heap_caps.h>

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
#include "games/StarFlux/StarFluxGame.h"
#include "games/TankFlux/TankFluxGame.h"
#include "games/TubeFlux/TubeFluxGame.h"

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
// GAMES, BUILT ON DEMAND
// A game object lives only while it's being played: built on launch, in
// internal RAM where there's room (it's the game's hot state; PSRAM is the
// fallback), and destroyed on the way back to the menu.
// =============================================================================
template <typename T>
IGame* makeGame(void*& mem) {
    const size_t size = sizeof(T) + alignof(T);
    mem = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!mem) mem = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    if (!mem) return nullptr;
    uintptr_t at = ((uintptr_t)mem + alignof(T) - 1) & ~(uintptr_t)(alignof(T) - 1);
    return new ((void*)at) T();
}

// =============================================================================
// LAUNCHER
// =============================================================================
LauncherMenu launcher;

// Game registry — order determines menu order
const GameEntry gameRegistry[] = {
    { "Asteroids", makeGame<AsteroidFluxGame>, "asteroids" },
    { "Lander",    makeGame<LanderFluxGame>,   "lander" },
    { "Maze",      makeGame<MazeFluxGame>,     "maze" },
    { "Runner",    makeGame<PlatformFluxGame>, "runner" },
    { "Star",      makeGame<StarFluxGame>,     "star" },
    { "Tank",      makeGame<TankFluxGame>,     "tank" },
    { "Tube",      makeGame<TubeFluxGame>,     "tube" },
    // Add future games here: { "New Game", makeGame<NewGame>, "newgame" },
};
const int GAME_COUNT = sizeof(gameRegistry) / sizeof(gameRegistry[0]);

// =============================================================================
// STATE
// =============================================================================
CabinetState cabinetState = STATE_LAUNCHER_MENU;
IGame*       activeGame   = nullptr;
void*        activeGameMem = nullptr;   // the block makeGame() allocated

// =============================================================================
// HELPERS
// =============================================================================

void returnToLauncher();

// Build the chosen game and switch to it: rotation, display, init.
void launchGame(int index) {
    Serial.printf("[CABINET] Free internal heap before launch: %u bytes\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    IGame* game = gameRegistry[index].create(activeGameMem);
    if (!game) {
        Serial.printf("[CABINET] Out of memory building %s.\n", gameRegistry[index].name);
        returnToLauncher();
        return;
    }
    activeGame = game;
    cabinetState = STATE_IN_GAME;
    game->setTFT(tft);
    uint8_t rotation = game->getRotation();
    tft.setRotation(rotation);
    input.waitForButtonARelease();  // Prevent launch-press bleeding into game
    // The launcher's launch melody gives way to the game's own start sound
    // (the old one-sound engine cut it off the same way).
    audio.mute();
    game->init(audio);
    Serial.printf("[CABINET] Launched: %s (rotation %d)\n", game->getName(), rotation);
}

void returnToLauncher() {
    if (activeGame) {
        activeGame->onExit();
        activeGame->~IGame();
        activeGame = nullptr;
    }
    if (activeGameMem) {
        heap_caps_free(activeGameMem);
        activeGameMem = nullptr;
    }
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
// Reported once a second, to the screen and to serial. On screen it reads
// "fps avgMs/peakMs" in magenta, low on the left — clear of every game's
// HUD text, though it does sit over the play area. Drawing it costs a
// little, counted against the next frame rather than the one reported.
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
    snprintf(buf, sizeof(buf), "%uf %u/%ums", (unsigned)fpsLastFps,
             (unsigned)((fpsLastAvgUs + 500) / 1000),
             (unsigned)((fpsLastPeakUs + 500) / 1000));

    // Sits above the bottom edge, where no game puts text: Tank Flux's
    // health bar is lower still, its radar is on the right, and the
    // portrait games keep this strip clear. tft.height() follows the
    // current rotation, so this lands correctly either way.
    const int16_t boxH = 9;
    const int16_t boxY = tft.height() - boxH - 9;
    const int16_t boxW = (int16_t)(strlen(buf) * 6 + 4);

    // One filled rect plus transparent text is far fewer pixel writes than
    // opaque text, which redraws every glyph's blank pixels too. This runs
    // every frame, so the difference is worth having.
    tft.fillRect(0, boxY, boxW, boxH, ArcadeConfig::COLOR_BLACK);
    tft.setFont();
    tft.setTextSize(1);
    tft.setTextColor(ArcadeConfig::COLOR_MAGENTA);
    tft.setCursor(2, boxY + 1);
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
            int pick = launcher.update(canvasPortrait, state, audio);
            tft.drawRGBBitmap(0, 0, canvasPortrait.getBuffer(),
                              ArcadeConfig::PORTRAIT_WIDTH, ArcadeConfig::PORTRAIT_HEIGHT);
            if (pick >= 0) launchGame(pick);
            break;
        }

        case STATE_IN_GAME: {
            // The canvas matching the game's rotation; the 2D games push it
            // to the display themselves, the rest are pushed here.
            GFXcanvas16& canvas = activeCanvas();
            bool running = activeGame->update(canvas, state, audio);
            if (!activeGame->flushesItself()) {
                tft.drawRGBBitmap(0, 0, canvas.getBuffer(), canvas.width(), canvas.height());
            }
            if (!running) returnToLauncher();
            break;
        }

        default:
            returnToLauncher();
            break;
    }

#ifdef SHOW_FPS
    fpsAccumulate(micros() - lastFrameUs);   // before the overlay's own cost
    fpsDraw();
#endif
}
