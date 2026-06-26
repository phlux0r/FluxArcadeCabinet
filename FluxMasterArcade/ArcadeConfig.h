#ifndef ARCADE_CONFIG_H
#define ARCADE_CONFIG_H

#include <Arduino.h>

// Global game state definitions
enum GameState {
    STATE_LAUNCHER_MENU,
    STATE_ASTEROID_FLUX,
    STATE_LANDER_FLUX
};

struct ArcadeConfig {
    // --- DISPLAY BUS (SPI) ---
    static const int TFT_CS   = 10;
    static const int TFT_RST  = 9;
    static const int TFT_DC   = 8;
    static const int TFT_BLK  = 7;  

    // --- SD CARD BUS (SHARED SPI) ---
    static const int SD_CS    = 2; 

    // --- MAX98357A AUDIO (I2S ON UNDERSIDE PADS) ---
    static const int I2S_DIN  = 14;
    static const int I2S_LRC  = 15;
    static const int I2S_BCLK = 16;

    // --- CABINET CONTROLS (JOYSTICK + 2 BUTTONS) ---
    static const int JOY_X       = 1;   // Horizontal analog steering
    static const int JOY_Y       = 17;  // Vertical analog movement (Underside Pad)
    static const int BUTTON_A    = 4;   // Primary microswitch (Select / Fire / Thrust)
    static const int BUTTON_B    = 21;  // Secondary microswitch (Back / Special / Cancel)

    // --- SCREEN GEOMETRY ---
    static const int SCREEN_WIDTH  = 128; 
    static const int SCREEN_HEIGHT = 160; 

    // --- ARCADE PALETTE ---
    static const uint16_t COLOR_BLACK   = 0x0000;
    static const uint16_t COLOR_WHITE   = 0xFFFF;
    static const uint16_t COLOR_GREEN   = 0x07E0;
    static const uint16_t COLOR_AMBER   = 0xFBE0;
    static const uint16_t COLOR_BLUE    = 0x001F;
};

#endif