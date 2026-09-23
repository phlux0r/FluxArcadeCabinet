#ifndef GAME_ENGINE_LANDER_H
#define GAME_ENGINE_LANDER_H

#include <Adafruit_GFX.h>
#include <Preferences.h>
#include "ArcadeConfig.h"
#include "Lander_CavernObstacles.h"
#include "Lander_ParticleEngine.h"
#include "Lander_Ship.h"

class GameEngineLander {
private:
    Preferences          _prefs;
    Lander_CavernObstacles _obstacles;
    Lander_ParticleEngine  _particles;
    Lander_Ship            _lander;

    const float THRUST_POWER = 0.09f;
    const float SAFE_LANDING_SPEED = 1.1f;

    int   _score = 0;
    int   _level = 1;
    int   _highScore = 0;
    bool  _isGameOver = false;
    bool  _isTitleScreen = true;
    
    unsigned long _attractModeTimer = 0;
    bool  _showInstructionPage = false;

    float _currentGravity = 0.04f;
    bool  _fuelTankActive = false;
    float _fuelTankX = 0.0f;
    float _fuelTankY = 0.0f;
    const int _fuelTankRadius = 4;

    static const int GROUND_SEGMENTS = 11;
    int   _groundY[GROUND_SEGMENTS];
    int   _groundStepX;
    int   _padX;
    int   _padWidth;

    void initLevel() {
        _lander.spawn();

        int gravityIncrements = (_level - 1) / 3;
        _currentGravity = 0.04f + (gravityIncrements * 0.01f);
        if (_currentGravity > 0.10f) _currentGravity = 0.10f;

        _obstacles.generateNewMap(_level);

        _padWidth = 26 - (_level * 2);
        if (_padWidth < 14) _padWidth = 14;
        _padX = random(10, ArcadeConfig::SCREEN_WIDTH - _padWidth - 10);

        for (int i = 0; i < GROUND_SEGMENTS; i++) {
            _groundY[i] = random(ArcadeConfig::SCREEN_HEIGHT - 32, ArcadeConfig::SCREEN_HEIGHT - 8);
        }

        int padStartSeg = _padX / _groundStepX;
        int padEndSeg = (_padX + _padWidth) / _groundStepX;
        for (int i = padStartSeg; i <= padEndSeg + 1 && i < GROUND_SEGMENTS; i++) {
            _groundY[i] = ArcadeConfig::SCREEN_HEIGHT - 12;
        }

        if (gravityIncrements >= 2) {
            _fuelTankActive = true;
            _fuelTankX = random(20, ArcadeConfig::SCREEN_WIDTH - 20);
            _fuelTankY = random(60, ArcadeConfig::SCREEN_HEIGHT - 50);
        } else {
            _fuelTankActive = false;
        }
    }

    void loadHighScore() {
        _prefs.begin("landerflux", true);
        _highScore = _prefs.getInt("highscore", 0);
        _prefs.end();
    }

    void saveHighScore() {
        _prefs.begin("landerflux", false);
        _prefs.putInt("highscore", _highScore);
        _prefs.end();
    }

    void renderTitleScreen(GFXcanvas16 &canvas) {
        canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
        canvas.drawRect(4, 4, ArcadeConfig::SCREEN_WIDTH - 8, ArcadeConfig::SCREEN_HEIGHT - 8, ArcadeConfig::COLOR_GREEN);
        
        canvas.setTextSize(2);
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.setCursor(12, 35);  canvas.print("LANDER");
        canvas.setTextColor(ArcadeConfig::COLOR_AMBER);
        canvas.setCursor(45, 55);  canvas.print("FLUX");

        canvas.setTextSize(1);
        canvas.setTextColor(0x7FFF); 
        canvas.setCursor(18, 90);  canvas.print("HI-SCORE: "); canvas.print(_highScore);

        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.setCursor(16, 125); canvas.print("[BTN A] LAUNCH");
    }

    void renderInstructionScreen(GFXcanvas16 &canvas) {
        canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
        canvas.drawRect(4, 4, ArcadeConfig::SCREEN_WIDTH - 8, ArcadeConfig::SCREEN_HEIGHT - 8, 0xF800);
        
        canvas.setTextSize(1);
        canvas.setTextColor(ArcadeConfig::COLOR_AMBER);
        canvas.setCursor(22, 20); canvas.print("PILOT DIRECTIVE");

        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.setCursor(12, 50);  canvas.print("- JOYSTICK: Steer");
        canvas.setCursor(12, 70);  canvas.print("- BUTTON A: Thrust");
        canvas.setCursor(12, 90);  canvas.print("- Green Landing Pad");
        canvas.setCursor(12, 110); canvas.print("- Watch Speed!");
    }

public:
    GameEngineLander() {
        _groundStepX = ArcadeConfig::SCREEN_WIDTH / (GROUND_SEGMENTS - 1);
    }

    void init() {
        loadHighScore();
        _score = 0;
        _level = 1;
        _lander.resetPools();
        _isTitleScreen = true;
        _isGameOver = false;
        _attractModeTimer = millis();
        initLevel();
    }

    bool update(GFXcanvas16 &canvas, bool btnA, bool btnB, int joyX, int joyY) {
        if (btnB) {
            return false; 
        }

        if (_isTitleScreen) {
            if (btnA) {
                _score = 0; _level = 1;
                _lander.resetPools();
                initLevel();
                _isTitleScreen = false;
                return true;
            }
            if (millis() - _attractModeTimer > 4000) {
                _showInstructionPage = !_showInstructionPage;
                _attractModeTimer = millis();
            }
            if (!_showInstructionPage) renderTitleScreen(canvas);
            else renderInstructionScreen(canvas);
            return true;
        }

        if (_isGameOver) {
            canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
            canvas.setTextSize(2);
            if (_lander.lives > 0) {
                canvas.setTextColor(ArcadeConfig::COLOR_GREEN);
                canvas.setCursor(16, 45); canvas.print("SUCCESS");
            } else {
                canvas.setTextColor(0xF800);
                canvas.setCursor(10, 45); canvas.print("GAME OVER");
            }
            
            canvas.setTextSize(1);
            canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
            canvas.setCursor(25, 85);  canvas.print("Final Score: "); canvas.print(_score);
            canvas.setCursor(15, 125); canvas.print("[BTN A] CONTINUE");

            if (btnA) {
                if (_lander.lives > 0) {
                    initLevel();
                } else {
                    init();
                }
                _isGameOver = false;
            }
            return true;
        }

        if (_lander.isDisintegrating) {
            _particles.update();
            if (millis() - _lander.explosionStartTime > 1300) {
                if (_lander.lives > 0) {
                    initLevel();
                } else {
                    if (_score > _highScore) { _highScore = _score; saveHighScore(); }
                    _isGameOver = true;
                }
            }
        } else {
            float targetAngle = map(joyX, 0, 4095, -45, 45) * (PI / 180.0f);
            _lander.updatePhysics(btnA, targetAngle, _currentGravity, THRUST_POWER, _particles);

            if (_fuelTankActive) {
                float dx = _lander.x - _fuelTankX;
                float dy = _lander.y - _fuelTankY;
                if ((dx * dx) + (dy * dy) < 64) {
                    _fuelTankActive = false;
                    _lander.fuel = min(100.0f, _lander.fuel + 40.0f);
                    _particles.triggerExplosion(_fuelTankX, _fuelTankY, 15);
                }
            }

            if (_obstacles.checkCollision(_lander.x, _lander.y, 3)) {
                _lander.kill(_particles);
            }

            int segmentIndex = (int)_lander.x / _groundStepX;
            segmentIndex = constrain(segmentIndex, 0, GROUND_SEGMENTS - 2);
            int segStartX = segmentIndex * _groundStepX;
            float percent = (float)((int)_lander.x - segStartX) / (float)_groundStepX;
            int floorY = _groundY[segmentIndex] + (percent * (_groundY[segmentIndex + 1] - _groundY[segmentIndex]));

            if (_lander.y >= floorY - 4) {
                _lander.y = floorY - 4;
                float landingSpeed = sqrt((_lander.vx * _lander.vx) + (_lander.vy * _lander.vy));
                bool overPad = ((int)_lander.x >= _padX && (int)_lander.x <= (_padX + _padWidth));

                if (overPad && landingSpeed < SAFE_LANDING_SPEED && abs(_lander.thrustAngle) < 0.2f) {
                    _score += (int)_lander.fuel + (_level * 50);
                    if (_score > _highScore) { _highScore = _score; saveHighScore(); }
                    _level++;
                    _isGameOver = true;
                } else {
                    _lander.kill(_particles);
                }
            }
        }

        canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
        _obstacles.render(canvas);
        _particles.update();
        _particles.render(canvas);

        if (_fuelTankActive) {
            canvas.drawTriangle(_fuelTankX, _fuelTankY - 5, _fuelTankX - 4, _fuelTankY + 1, _fuelTankX + 4, _fuelTankY + 1, 0x5DFF);
            canvas.drawTriangle(_fuelTankX, _fuelTankY + 5, _fuelTankX - 4, _fuelTankY - 1, _fuelTankX + 4, _fuelTankY - 1, 0x5DFF);
        }

        for (int x = 0; x < ArcadeConfig::SCREEN_WIDTH; x++) {
            int segment = x / _groundStepX;
            segment = constrain(segment, 0, GROUND_SEGMENTS - 2);
            int sx = segment * _groundStepX;
            float p = (float)(x - sx) / (float)_groundStepX;
            int exactY = _groundY[segment] + (p * (_groundY[segment + 1] - _groundY[segment]));
            canvas.drawFastVLine(x, exactY, ArcadeConfig::SCREEN_HEIGHT - exactY, 0x9300);
        }

        canvas.fillRect(_padX, ArcadeConfig::SCREEN_HEIGHT - 14, _padWidth, 3, ArcadeConfig::COLOR_GREEN);

        _lander.render(canvas, btnA, SAFE_LANDING_SPEED);

        // --- COMPACT HUD OVERLAY WITH WHOLE NUMBER GRAVITY TIER ---
        canvas.setTextSize(1); 
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        
        canvas.setCursor(1, 2);   
        canvas.print("S:"); canvas.print(_score);
        
        canvas.setCursor(38, 2);  
        canvas.print("F:"); canvas.print((int)_lander.fuel); canvas.print("%");
        
        canvas.setCursor(76, 2); 
        canvas.print("L:"); canvas.print(_lander.lives);

        // Calculate whole number gravity tier (Level 1-3 = G:1, Level 4-6 = G:2, etc.)
        int gravityTier = ((_level - 1) / 3) + 1;
        canvas.setCursor(105, 2);
        canvas.print("G:"); canvas.print(gravityTier);

        return true;
    }
};

#endif