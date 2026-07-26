#ifndef ARCADE_CONFIG_H
#define ARCADE_CONFIG_H

#include <Arduino.h>

// =============================================================================
// FLUX ARCADE CABINET — HARDWARE CONFIGURATION
// Single source of truth for all pin assignments and hardware constants.
// No game or subsystem should define its own pins — reference these only.
// =============================================================================

struct ArcadeConfig {

    // -------------------------------------------------------------------------
    // DISPLAY BUS (SPI)
    // -------------------------------------------------------------------------
    static const int TFT_CS   = 10;
    static const int TFT_RST  = 9;
    static const int TFT_DC   = 8;
    static const int TFT_BLK  = 7;

    static const uint32_t TFT_SPI_SPEED = 27000000UL;
    static const uint32_t SPI_BUS_SPEED = 40000000UL;

    // -------------------------------------------------------------------------
    // SD CARD (SHARED SPI BUS)
    // -------------------------------------------------------------------------
    static const int SD_CS = 2;

    // -------------------------------------------------------------------------
    // MAX98357A AUDIO (I2S)
    // -------------------------------------------------------------------------
    static const int I2S_BCLK = 16;
    static const int I2S_LRC  = 15;
    static const int I2S_DIN  = 14;

    static const int I2S_SAMPLE_RATE    = 44100;
    static const int I2S_BITS_PER_SAMPLE = 16;

    // -------------------------------------------------------------------------
    // CABINET CONTROLS
    // -------------------------------------------------------------------------
    static const int JOY_X    = 1;
    static const int JOY_Y    = 17;
    static const int BUTTON_A = 4;
    static const int BUTTON_B = 21;

    static const int JOY_CENTER       = 2048;
    static const int JOY_DEADZONE     = 200;
    static const int JOY_THRESHOLD    = 1000;
    static const int BUTTON_DEBOUNCE_MS = 30;

    // -------------------------------------------------------------------------
    // SCREEN DIMENSIONS
    // Physical display is 128x160. Rotation changes which axis is which.
    // Use LANDSCAPE_ for Asteroid Flux (rotation 1), PORTRAIT_ for everything else.
    // The legacy SCREEN_WIDTH/HEIGHT aliases below keep copied game files
    // compiling without modification — they intentionally match the context
    // each game uses (landscape for Asteroid, portrait for Lander).
    // -------------------------------------------------------------------------
    static const int PORTRAIT_WIDTH   = 128;
    static const int PORTRAIT_HEIGHT  = 160;

    static const int LANDSCAPE_WIDTH  = 160;
    static const int LANDSCAPE_HEIGHT = 128;

    static const int CANVAS_MAX_W = 160;
    static const int CANVAS_MAX_H = 160;

    // -------------------------------------------------------------------------
    // SHARED UI COLOURS (RGB565)
    // -------------------------------------------------------------------------
    static const uint16_t COLOR_BLACK    = 0x0000;
    static const uint16_t COLOR_WHITE    = 0xFFFF;
    static const uint16_t COLOR_AMBER    = 0xFBE0;
    static const uint16_t COLOR_GREEN    = 0x07E0;
    static const uint16_t COLOR_RED      = 0xF800;
    static const uint16_t COLOR_CYAN     = 0x07FF;
    static const uint16_t COLOR_BLUE     = 0x001F;
    static const uint16_t COLOR_ION_BLUE = 0x041F;
    static const uint16_t COLOR_GREY     = 0x7BEF;
    static const uint16_t COLOR_MAGENTA  = 0xF81F;
    static const uint16_t COLOR_ORANGE   = 0xFD20;
    static const uint16_t COLOR_YELLOW   = 0xFFE0;

    // -------------------------------------------------------------------------
    // FRAME TIMING
    // -------------------------------------------------------------------------
    static const uint32_t FRAME_INTERVAL_US = 16666UL;

    // -------------------------------------------------------------------------
    // ATTRACT MODE
    // -------------------------------------------------------------------------
    static const unsigned long ATTRACT_MODE_TIMER = 6000UL;  // ms per attract slide

    // =========================================================================
    // ASTEROID FLUX — GAME CONSTANTS
    // Moved here from the old GameConfig.h so AsteroidManager, PowerUpManager
    // etc. compile without modification beyond the #include swap.
    // =========================================================================

    // Screen aliases — Asteroid Flux is landscape
    static const int SCREEN_WIDTH  = LANDSCAPE_WIDTH;
    static const int SCREEN_HEIGHT = LANDSCAPE_HEIGHT;
    static const int UI_MARGIN_TOP = 11;

    // Ship dimensions
    static const int SHIP_WIDTH        = 16;
    static const int SHIP_HEIGHT       = 10;
    static const int SHIP_ANIM_SPEED_MS = 80;

    // Difficulty
    static const int   MAX_ASTEROIDS        = 6;
    static const int   SCORE_TO_SPAWN       = 10;
    static constexpr float BASE_SPEED       = 1.1f;
    static constexpr float SPEED_STEP       = 0.07f;
    static constexpr float COMET_SPEED_CAP  = 6.0f;
    static const int   COMET_BONUS_SCORE    = 15;

    // Power-ups
    static const int   POWERUP_START_SCORE      = 100;
    static const int   MIN_SCORE_FOR_EXTRA_LIFE = 600;
    static const int   SHIELD_DURATION_MS       = 10000;
    static const int   EXTRA_LIFE_CHANCE        = 30;
    static const int   POWERUP_SPAWN_LOW_MS     = 15000;
    static const int   POWERUP_SPAWN_HIGH_MS    = 40000;
    static const int   SLOW_SPEED_CHANCE        = 60;
    static const int   SPEED_STEPS_TO_REDUCE    = 4;

    // Power-up colours
    static const uint16_t COLOR_SHIELD = 0x07E0;   // Green
    static const uint16_t COLOR_HEALTH = 0xF81F;   // Magenta
    static const uint16_t COLOR_SLOW   = 0x07FF;   // Cyan

    // Background
    static const int   MAX_STARS              = 16;
    static constexpr float STAR_SCROLL_SPEED  = 0.25f;

    // Nebula
    static constexpr float NEBULA_SCROLL_SPEED = 0.05f;
    static const uint16_t  COLOR_NEBULA        = 0x2087;

    // Particles
    static const int MAX_PARTICLES        = 30;
    static const int PARTICLE_LIFESPAN_MS = 600;

    // Maze Flux
    static const int MAZE_TIME_LEFT       = 500;
};

// -------------------------------------------------------------------------
// CABINET STATE MACHINE
// Add a new entry here when adding a new game.
// -------------------------------------------------------------------------
enum CabinetState {
    STATE_LAUNCHER_MENU,
    STATE_ASTEROID_FLUX,
    STATE_LANDER_FLUX,
    STATE_MAZE_FLUX
    // STATE_NEW_GAME  <-- add future games here
};

#endif // ARCADE_CONFIG_H