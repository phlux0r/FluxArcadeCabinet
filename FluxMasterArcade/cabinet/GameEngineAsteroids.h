#ifndef GAME_ENGINE_ASTEROIDS_H
#define GAME_ENGINE_ASTEROIDS_H

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Preferences.h>
#include <Fonts/TomThumb.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include "ArcadeConfig.h"
#include "AudioEngineAsteroids.h"
#include "PlayerShip.h"
#include "PowerUpManager.h"
#include "AsteroidManager.h"
#include "../../cabinet/ParticleManager.h"
#include "BackgroundStars.h"
#include "NebulaManager.h"

// Binary Memory Assets
#include "splash_image.h"
#include "explosion.h"
#include "gamestart.h"
#include "gameend.h"

// --- ADD THIS LINE RIGHT HERE ---
extern GFXcanvas16 canvas;

class GameEngineAsteroids {
private:
    Adafruit_ST7735& _tft;
    Preferences      _prefs;

    AudioEngineAsteroids _audio;
    PlayerShip           _ship;
    PowerUpManager       _powerUps;
    AsteroidManager      _asteroids;
    BackgroundStars      _background;
    NebulaManager        _nebula;
    ParticleManager      _particles;

    int _score = 0;
    int _highScore = 0; 
    int _lives = 3;
    int _asteroidsPassed = 0;
    int _nextTargetScore = 10;

    enum AttractScreen { SCREEN_SPLASH, SCREEN_INFO };
    AttractScreen _currentAttractScreen = SCREEN_SPLASH;

    unsigned long _lastScreenSwitchTime = 0;
    const unsigned long ATTRACT_SLIDE_DURATION = 8000;

    bool _isGameRunning = false;
    bool _isGameOverMode = false;
    bool _isCountdownActive = false;

    unsigned long _countdownStartTime = 0;
    int _countdownValue = 3;

    void initNewGame(GFXcanvas16 &canvas) {
        _score = 0;
        _lives = 3;
        _asteroidsPassed = 0;
        _nextTargetScore = 10; 
        _ship.reset();
        _powerUps.resetTimeline();
        _particles.clearAll();
        _asteroids.initGame();

        _tft.fillScreen(ST7735_BLACK);
        drawUI(canvas);
        startCountdown();
    }

    void drawUI(GFXcanvas16 &canvas) {
        canvas.drawFastHLine(0, 11 - 1, ArcadeConfig::SCREEN_WIDTH, ST7735_GREEN); 
        canvas.fillRect(0, 0, ArcadeConfig::SCREEN_WIDTH, 11 - 2, ST7735_BLACK);
        
        canvas.setTextSize(1);
        canvas.setTextColor(ST7735_YELLOW);
        canvas.setCursor(4, 1);
        canvas.print("SCORE:"); canvas.print(_score);
        
        canvas.setTextColor(0x7FFF); 
        canvas.setCursor(ArcadeConfig::SCREEN_WIDTH / 2 - 10, 1);
        canvas.print("HI:"); canvas.print(_highScore);
        
        canvas.setCursor(ArcadeConfig::SCREEN_WIDTH - 40, 1);
        canvas.setTextColor(ST7735_RED);
        canvas.print(_lives); canvas.print(" UP");
    }

    void startCountdown() {
        _isCountdownActive = true;
        _countdownValue = 3;
        _countdownStartTime = millis();
        _audio.playSound(800, 100);
    }

    void updateCountdown(GFXcanvas16 &canvas) {
        unsigned long elapsed = millis() - _countdownStartTime;
        
        if (elapsed >= 750) { 
            _countdownValue--;
            _countdownStartTime = millis();
            
            if (_countdownValue > 0) {
                _audio.playSound(800, 100);
            } else {
                _isCountdownActive = false; 
                _isGameRunning = true;
                return;
            }
        }

        canvas.fillRect(0, 11, ArcadeConfig::SCREEN_WIDTH, ArcadeConfig::SCREEN_HEIGHT - 11, ST7735_BLACK);
        canvas.setCursor(ArcadeConfig::SCREEN_WIDTH / 2 - 6, ArcadeConfig::SCREEN_HEIGHT / 2 - 8);
        canvas.setTextColor(ST7735_CYAN);
        canvas.setTextSize(2);
        canvas.print(_countdownValue);
    }

    void triggerShipExplosion() {
        float centerX = _ship.getX() + 8.0f; 
        float centerY = (float)_ship.getY() + 5.5f; 
        _particles.spawnExplosion(centerX, centerY, ST7735_CYAN, 30);
        _tft.drawRGBBitmap(0, 0, canvas.getBuffer(), 160, 128); // Dynamic resolution horizontal buffer clear
        _audio.playSample(explosion_data, sizeof(explosion_data));
    }

    void gameOverSequence(GFXcanvas16 &canvas) {
        _isGameRunning = false;
        _isGameOverMode = true;
        
        bool newRecord = false;
        if (_score > _highScore) {
            _highScore = _score;
            newRecord = true;
            _prefs.begin("game_data", false);
            _prefs.putInt("highscore", _highScore);
            _prefs.end();
        }

        _tft.fillScreen(ST7735_BLACK);
        _audio.playSample(gameend_data, sizeof(gameend_data));

        _tft.setTextSize(1);
        _tft.setCursor(ArcadeConfig::SCREEN_WIDTH / 4, 45);
        _tft.setTextColor(ST7735_WHITE);
        _tft.print("YOUR SCORE: ");
        _tft.setTextColor(ST7735_YELLOW);
        _tft.print(_score);

        _tft.setCursor(ArcadeConfig::SCREEN_WIDTH / 4, 65);
        if (newRecord) {
            _tft.setTextColor(ST7735_GREEN);
            _tft.print("NEW HIGH SCORE!! ");
        } else {
            _tft.setTextColor(0x7FFF);
            _tft.print("HIGH SCORE: "); _tft.print(_highScore);
        }

        _tft.setCursor(ArcadeConfig::SCREEN_WIDTH / 4 - 28, 95);
        _tft.setTextColor(ST7735_CYAN);
        _tft.print("> TWIST DIAL TO RESET <");
        
        _audio.playGameOverMelody();
    }

    void showWelcomeSplashScreen() {
        _tft.fillScreen(ST7735_BLACK);
        for (int i = 0; i < 20480; i++) {
            uint8_t byte1 = splash_bitmap[i * 2];
            uint8_t byte2 = splash_bitmap[i * 2 + 1];
            uint16_t correctedPixel = (byte2 << 8) | byte1;
            _tft.drawPixel(i % ArcadeConfig::SCREEN_WIDTH, i / ArcadeConfig::SCREEN_WIDTH, correctedPixel);
        }

        _tft.setTextSize(1);
        _tft.setCursor(ArcadeConfig::SCREEN_WIDTH / 4 - 4, 94);
        _tft.setTextColor(0x7FFF);
        _tft.print("BEST RECORD: ");
        _tft.setTextColor(ST7735_YELLOW);
        _tft.print(_highScore);

        _tft.setCursor(18, 115);
        _tft.setTextColor(ST7735_CYAN);
        _tft.print("MOVE CONTROL TO START");
        
        _audio.playSample(gamestart_data, sizeof(gamestart_data));
        _audio.playStartupMelody();
    }

    void drawInfoScreen() {
        _tft.fillScreen(ST7735_BLACK);
        _tft.setTextSize(1);
        _tft.setTextColor(ST7735_WHITE);
        _tft.setCursor(8, 6);
        _tft.print("---== GAME REWARDS ==---");
        _tft.setCursor(20, 64);
        _tft.print("---== POWERUPS ==---");

        _tft.setFont(&TomThumb);
        _tft.setTextSize(1);
        
        _tft.setTextColor(ST7735_YELLOW); _tft.setCursor(15, 25); _tft.print("TINY:       +1 PTS");
        _tft.setTextColor(ST7735_CYAN);   _tft.setCursor(95, 25); _tft.print("SMALL: +2 PTS");
        _tft.setTextColor(0xFBE0);         _tft.setCursor(15, 35); _tft.print("MEDIUM:   +3 PTS");
        _tft.setTextColor(ST7735_RED);    _tft.setCursor(95, 35); _tft.print("LARGE: +4 PTS");
        _tft.setTextColor(ST7735_BLUE);   _tft.setCursor(15, 45); _tft.print("MASSIVE: +5 PTS");
        _tft.setTextColor(ST7735_MAGENTA);_tft.setCursor(95, 45); _tft.print("COMET: +15 PTS");

        _tft.drawFastHLine(0, 55, ArcadeConfig::SCREEN_WIDTH, 0x4208);

        _tft.setTextColor(ST7735_CYAN);  _tft.setCursor(5, 85); _tft.print("CLOCK:    ");
        _tft.setTextColor(ST7735_WHITE); _tft.print("SLOWS DOWN SECTOR");

        _tft.setTextColor(ST7735_GREEN); _tft.setCursor(5, 95); _tft.print("SHIELD:  ");
        _tft.setTextColor(ST7735_WHITE); _tft.print("ABSORBS 1 COLLISION");

        _tft.setTextColor(ST7735_MAGENTA); _tft.setCursor(5, 105); _tft.print("HEART:    ");
        _tft.setTextColor(ST7735_WHITE);  _tft.print("BONUS SHIP ACCESS");

        _tft.setFont(); // Reset default font
    }

public:
    GameEngineAsteroids(Adafruit_ST7735& tftInstance) : _tft(tftInstance) {}

    void init() {
        _prefs.begin("game_data", false);
        _highScore = _prefs.getInt("highscore", 0);
        _prefs.end();

        _isGameRunning = false;
        _isGameOverMode = false;
        _isCountdownActive = false;
        _currentAttractScreen = SCREEN_SPLASH;
        _lastScreenSwitchTime = millis();

        _tft.setRotation(1); // Set horizontal widescreen mode
        showWelcomeSplashScreen();
    }

    bool update(GFXcanvas16 &canvas, bool btnA, bool btnB, int joyX, int joyY) {
        // Core Launcher Return Call
        if (btnB) {
            _audio.mute();
            return false; 
        }

        // 1. ATTRACT SYSTEM RUNTIME LOOP
        if (!_isGameRunning && !_isGameOverMode && !_isCountdownActive) {
            if (btnA) {
                initNewGame(canvas);
                return true;
            }

            if (millis() - _lastScreenSwitchTime >= ATTRACT_SLIDE_DURATION) {
                _lastScreenSwitchTime = millis();
                if (_currentAttractScreen == SCREEN_SPLASH) {
                    _currentAttractScreen = SCREEN_INFO;
                    drawInfoScreen();
                } else {
                    _currentAttractScreen = SCREEN_SPLASH;
                    showWelcomeSplashScreen();
                }
            }
            return true;
        }

        // 2. INTERMISSION MODE GAME OVER OVERLAY
        if (_isGameOverMode) {
            if (btnA) {
                _isGameOverMode = false;
                showWelcomeSplashScreen();
            }
            return true;
        }

        // 3. COUNTDOWN FLIGHT GATING
        if (_isCountdownActive) {
            updateCountdown(canvas);
            _tft.drawRGBBitmap(0, 0, canvas.getBuffer(), ArcadeConfig::SCREEN_WIDTH, ArcadeConfig::SCREEN_HEIGHT);
            return true;
        }

        // 4. MAIN INTERACTIVE GAMEPLAY ENGINE LOOP
        bool uiNeedsUpdate = false;
        bool playerHit = false;

        _audio.update();
        _nebula.update();
        _background.update();
        _particles.update();
        
        // Pass input directly to the internal managers instead of accessing globals
        _ship.updatePosition(joyX, joyY); 
        _ship.updateAnimation();
        _ship.updateShield();

        _powerUps.update(_score, _ship, _lives, uiNeedsUpdate, _audio, _asteroids);
        _asteroids.update(_ship, _score, _asteroidsPassed, _nextTargetScore, uiNeedsUpdate, playerHit, _audio, _particles);

        if (playerHit) {
            triggerShipExplosion();
            _lives--;
            delay(300);
            if (_lives <= 0) {
                gameOverSequence(canvas);
                return true;
            } else {
                _tft.fillScreen(ST7735_BLACK);
                drawUI(canvas);
                _asteroids.forceBoardWipe();
                _particles.clearAll();
                startCountdown();
                return true;
            }
        }

        // Assemble Virtual Memory Canvas Frame Layers
        canvas.fillRect(0, 11, ArcadeConfig::SCREEN_WIDTH, ArcadeConfig::SCREEN_HEIGHT - 11, ST7735_BLACK);
        _nebula.render(canvas);
        _background.render(canvas);
        _particles.render(canvas);
        _powerUps.render(canvas);
        _asteroids.render(canvas);
        _ship.render(canvas);

        if (uiNeedsUpdate || (_score == 0 && _lives == 3)) {
            drawUI(canvas);
        }

        _tft.drawRGBBitmap(0, 0, canvas.getBuffer(), ArcadeConfig::SCREEN_WIDTH, ArcadeConfig::SCREEN_HEIGHT);
        return true;
    }
};

#endif