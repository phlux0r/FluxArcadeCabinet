#ifndef GAME_ENGINE_MAZE_H
#define GAME_ENGINE_MAZE_H

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Preferences.h>
#include "../../cabinet/ArcadeConfig.h"
#include "../../cabinet/AudioEngine.h"
#include "../../cabinet/ParticleManager.h"
#include "MazeGenerator.h"
#include "MazeRenderer.h"
#include "PlayerMaze.h"
#include "Obstacles.h"
#include "Collectibles.h"
#include "SpriteManager.h"

class GameEngineMaze {
private:
    Adafruit_ST7735* _tft = nullptr;
    Preferences      _prefs;

    MazeGenerator   _maze;
    MazeRenderer    _renderer;
    PlayerMaze      _player;
    ParticleManager _particles;

    static const int MAX_BOMBS     = 6;
    static const int MAX_TRAPS     = 4;
    static const int MAX_KEYS      = 4;
    static const int MAX_DOORS     = 4;
    static const int MAX_TELEPORTS = 4;
    static const int MAX_BOOSTS    = 2;
    static const int MAX_BONUSES   = 2;

    ProximityBomb _bombs[MAX_BOMBS];
    TrapEmitter   _traps[MAX_TRAPS];
    Key           _keys[MAX_KEYS];
    Door          _doors[MAX_DOORS];
    SpeedBoost    _boosts[MAX_BOOSTS];
    TimeBonus     _bonuses[MAX_BONUSES];
    Exit          _exit;
    Teleport      _teleports[MAX_TELEPORTS];

    int _activeBombs     = 0;
    int _activeTraps     = 0;
    int _activeKeys      = 0;
    int _activeDoors     = 0;
    int _activeTeleports = 0;

    enum GameState { STATE_TITLE, STATE_INSTRUCTIONS, STATE_PLAYING, STATE_GAMEOVER, STATE_LEVEL_COMPLETE };
    GameState _state = STATE_TITLE;

    int  _level     = 1;
    int  _score     = 0;
    int  _highScore = 0;
    int  _floor     = 0;
    int  _timeLeft  = ArcadeConfig::MAZE_TIME_LEFT;

    unsigned long _lastSecondMs = 0;
    unsigned long _attractTimer = 0;
    unsigned long _gameOverMs   = 0;

    static const unsigned long ATTRACT_INTERVAL_MS = 8000UL;
    static const unsigned long GAMEOVER_TIMEOUT_MS = 30000UL;
    static const int           TIME_BONUS_SECONDS  = 15;

    bool _btnAWasHeld      = false;
    bool _showInstructions = false;

    int _camX = 0;
    int _camY = 0;

    void saveHighScore() {
        _prefs.begin("maze_flux", false);
        _prefs.putInt("high_score", _highScore);
        _prefs.end();
    }

    // -------------------------------------------------------------------------
    // Level setup
    // -------------------------------------------------------------------------
    void initLevel() {
        int w = 16, h = 20;
        if (_level >= 11) {
            w = constrain(16 + (_level - 11) * 4, 16, 50);
            h = constrain(20 + (_level - 11) * 4, 20, 50);
        }
        _maze.generate(w, h);

        _player.reset(0, 0);
        _particles.clearAll();
        _camX = 0; _camY = 0;
        _timeLeft     = ArcadeConfig::MAZE_TIME_LEFT + (_level * 10);
        _lastSecondMs = millis();

        _activeBombs = _activeTraps = _activeKeys = _activeDoors = _activeTeleports = 0;
        for (auto &b : _boosts)  b.collected = false;
        for (auto &b : _bonuses) b.collected = false;

        _exit.x = _maze.width - 1;
        _exit.y = _maze.height - 1;
        _exit.unlocked = false;

        _placeKeys();
        _placeDoors();
        _placeBombs();
        _placeTraps();
        _placePowerups();
        _placeTeleports();
    }

    // -------------------------------------------------------------------------
    // Reachability flood fill from (0,0)
    // -------------------------------------------------------------------------
    bool _isReachable(int tx, int ty) {
        static bool visited[2500];
        static int  qx[2500];
        static int  qy[2500];
        memset(visited, 0, sizeof(visited));

        int head = 0, tail = 0;
        qx[tail] = 0; qy[tail] = 0; tail++;
        visited[0] = true;

        const uint8_t dirs[] = { WALL_N, WALL_S, WALL_E, WALL_W };
        const int     ddx[]  = { 0, 0, 1, -1 };
        const int     ddy[]  = { -1, 1, 0, 0 };

        while (head < tail) {
            int x = qx[head], y = qy[head]; head++;
            if (x == tx && y == ty) return true;
            for (int i = 0; i < 4; i++) {
                if (_maze.hasWall(x, y, dirs[i])) continue;
                int nx = x + ddx[i], ny = y + ddy[i];
                if (nx < 0 || nx >= _maze.width || ny < 0 || ny >= _maze.height) continue;
                int idx = ny * _maze.width + nx;
                if (visited[idx]) continue;
                visited[idx] = true;
                qx[tail] = nx; qy[tail] = ny; tail++;
            }
        }
        return false;
    }

    // -------------------------------------------------------------------------
    // Placement helpers
    // -------------------------------------------------------------------------
    bool _isTileFree(int x, int y) {
        if (x == 1 && y == 1) return false;
        if (x == _exit.x && y == _exit.y) return false;
        if (!_isReachable(x, y)) return false;
        for (int i = 0; i < _activeKeys;  i++) if (_keys[i].x  == x && _keys[i].y  == y) return false;
        for (int i = 0; i < _activeDoors; i++) if (_doors[i].x == x && _doors[i].y == y) return false;
        for (int i = 0; i < _activeBombs; i++) if (_bombs[i].x == x && _bombs[i].y == y) return false;
        return true;
    }

    void _placeKeys() {
        int keyCount = (_level >= 9) ? constrain((_level - 8), 1, MAX_KEYS) : 1;
        _activeKeys = 0;
        uint8_t colours[] = { 1, 2, 3, 4 };
        for (int k = 0; k < keyCount && _activeKeys < MAX_KEYS; k++) {
            for (int attempt = 0; attempt < 50; attempt++) {
                int tx = random(1, _maze.width);
                int ty = random(1, _maze.height);
                if (_isTileFree(tx, ty)) {
                    _keys[_activeKeys++] = { tx, ty, colours[k], false };
                    break;
                }
            }
        }
    }

    void _placeDoors() {
        if (_level < 9) return;
        _activeDoors = 0;
        for (int k = 0; k < _activeKeys && _activeDoors < MAX_DOORS; k++) {
            for (int attempt = 0; attempt < 50; attempt++) {
                int tx = random(1, _maze.width);
                int ty = random(1, _maze.height);
                if (_isTileFree(tx, ty)) {
                    _doors[_activeDoors++] = { tx, ty, _keys[k].colourId, false };
                    break;
                }
            }
        }
    }

    void _placeBombs() {
        bool hasBombs = (_level >= 3 && _level <= 4) || (_level >= 7 && _level != 9 && _level != 10);
        if (!hasBombs) return;
        int count = constrain(_level / 3, 1, MAX_BOMBS);
        _activeBombs = 0;
        for (int i = 0; i < count && _activeBombs < MAX_BOMBS; i++) {
            for (int attempt = 0; attempt < 50; attempt++) {
                int tx = random(1, _maze.width);
                int ty = random(1, _maze.height);
                if (_isTileFree(tx, ty)) {
                    _bombs[_activeBombs].init(tx, ty);
                    _activeBombs++;
                    break;
                }
            }
        }
    }

    void _placeTraps() {
        _activeTraps = 0;
        bool hasTypeA = (_level >= 5 && _level <= 8) || _level >= 11;
        bool hasTypeB = (_level >= 9);

        if (hasTypeA) {
            for (int attempt = 0; attempt < 50 && _activeTraps < MAX_TRAPS; attempt++) {
                int tx = random(1, _maze.width - 1);
                int ty = random(1, _maze.height - 1);
                if (!_isTileFree(tx, ty)) continue;
                _traps[_activeTraps].init(tx, ty, 1, 0, TrapEmitter::TYPE_A, random(3, 6));
                _activeTraps++;
            }
        }
        if (hasTypeB) {
            for (int attempt = 0; attempt < 50 && _activeTraps < MAX_TRAPS; attempt++) {
                int tx = random(1, _maze.width - 1);
                int ty = random(1, _maze.height - 1);
                if (!_isTileFree(tx, ty)) continue;
                _traps[_activeTraps].init(tx, ty, 0, 1, TrapEmitter::TYPE_B, random(3, 6));
                _activeTraps++;
            }
        }
    }

    void _placePowerups() {
        for (int i = 0; i < 2; i++) {
            for (int attempt = 0; attempt < 50; attempt++) {
                int tx = random(1, _maze.width);
                int ty = random(1, _maze.height);
                if (_isTileFree(tx, ty)) {
                    _boosts[i]  = { tx, ty, false };
                    _bonuses[i] = { tx + 1, ty, TIME_BONUS_SECONDS, false };
                    break;
                }
            }
        }
    }

    void _placeTeleports() {
        if (_level < 11) return;
        _activeTeleports = 0;
        for (int attempt = 0; attempt < 50 && _activeTeleports < MAX_TELEPORTS; attempt++) {
            int tx = random(1, _maze.width);
            int ty = random(1, _maze.height);
            if (_isTileFree(tx, ty)) {
                _teleports[_activeTeleports++] = { tx, ty, _floor + 1, 0, 0, true };
                break;
            }
        }
    }

    // -------------------------------------------------------------------------
    // Camera — centres on player, accounts for HUD
    // -------------------------------------------------------------------------
    void updateCamera() {
        int tilesX = ArcadeConfig::PORTRAIT_WIDTH / MazeRenderer::TILE;
        int tilesY = (ArcadeConfig::PORTRAIT_HEIGHT - MazeRenderer::HUD_HEIGHT) / MazeRenderer::TILE;
        _camX = constrain(_player.x - tilesX / 2, 0, max(0, _maze.width  - tilesX));
        _camY = constrain(_player.y - tilesY / 2, 0, max(0, _maze.height - tilesY));
    }

    // -------------------------------------------------------------------------
    // Interactions
    // -------------------------------------------------------------------------
    void checkInteractions(AudioEngine &audio) {
        int px = _player.x, py = _player.y;

        for (int i = 0; i < _activeKeys; i++) {
            if (!_keys[i].collected && _keys[i].x == px && _keys[i].y == py) {
                _keys[i].collected = true;
                audio.playPowerUpExtraLife();
                for (int d = 0; d < _activeDoors; d++)
                    _doors[d].tryOpen(_keys[i].colourId);
                bool allCollected = true;
                for (int k = 0; k < _activeKeys; k++)
                    if (!_keys[k].collected) { allCollected = false; break; }
                if (allCollected) _exit.unlocked = true;
            }
        }

        if (_exit.unlocked && px == _exit.x && py == _exit.y) {
            _score += _timeLeft;
            if (_score > _highScore) { _highScore = _score; saveHighScore(); }
            _level++;
            _state = STATE_LEVEL_COMPLETE;
            _gameOverMs = millis();
            audio.playLandingSuccessSound();
        }

        for (auto &b : _boosts) {
            if (!b.collected && b.x == px && b.y == py) {
                b.collected = true;
                _player.applySpeedBoost();
                audio.playTone(880, 100);
            }
        }

        for (auto &b : _bonuses) {
            if (!b.collected && b.x == px && b.y == py) {
                b.collected = true;
                _timeLeft += b.bonusSeconds;
                audio.playTone(660, 100);
            }
        }

        for (int i = 0; i < _activeTeleports; i++) {
            if (_teleports[i].active && _teleports[i].x == px && _teleports[i].y == py) {
                _floor = _teleports[i].destFloor;
                _player.reset(_teleports[i].destX, _teleports[i].destY);
                audio.playTone(440, 200);
                initLevel();
                return;
            }
        }

        if (_player.interact) {
            for (int i = 0; i < _activeTraps; i++) {
                if (_traps[i].type == TrapEmitter::TYPE_B) {
                    int dx = abs(_traps[i].x - px);
                    int dy = abs(_traps[i].y - py);
                    if (dx <= 1 && dy <= 1) {
                        _traps[i].activateSwitch();
                        audio.playTone(523, 80);
                    }
                }
            }
        }

        for (int i = 0; i < _activeBombs; i++) {
            bool exploded = _bombs[i].update(px, py);
            if (exploded) {
                for (int p = 0; p < 20; p++)
                    _particles.spawnFire(
                        _bombs[i].x * MazeRenderer::TILE,
                        _bombs[i].y * MazeRenderer::TILE,
                        random(-10, 10) * 0.1f,
                        random(-10, 10) * 0.1f);
                audio.playExplosionSound(explosion_data, sizeof(explosion_data));
                _player.alive = false;
            }
        }

        for (int i = 0; i < _activeTraps; i++) {
            if (_traps[i].checkBulletHit(px, py)) {
                _player.alive = false;
                audio.playExplosionSound(explosion_data, sizeof(explosion_data));
            }
        }

        if (!_player.alive) {
            _player.lives--;
            if (_player.lives <= 0) {
                if (_score > _highScore) { _highScore = _score; saveHighScore(); }
                _state      = STATE_GAMEOVER;
                _gameOverMs = millis();
            } else {
                _player.reset(0, 0);
            }
        }
    }

    // -------------------------------------------------------------------------
    // Render
    // -------------------------------------------------------------------------
    void renderPlaying(GFXcanvas16 &canvas) {
        _renderer.draw(canvas, _maze, _camX, _camY);
        _particles.render(canvas);

        const int tile    = MazeRenderer::TILE;
        const int camPixX = _camX * tile;
        const int camPixY = _camY * tile - MazeRenderer::HUD_HEIGHT;  // HUD_HEIGHT already baked into renderer

        // Keys
        for (int i = 0; i < _activeKeys; i++) {
            if (_keys[i].collected) continue;
            int sx = _keys[i].x * tile - camPixX;
            int sy = _keys[i].y * tile - camPixY;
            canvas.fillRect(sx + 2, sy + 2, 4, 4, ArcadeConfig::COLOR_YELLOW);
        }

        // Doors
        for (int i = 0; i < _activeDoors; i++) {
            if (_doors[i].open) continue;
            int sx = _doors[i].x * tile - camPixX;
            int sy = _doors[i].y * tile - camPixY;
            canvas.fillRect(sx, sy + 2, 8, 4, ArcadeConfig::COLOR_RED);
        }

        // Exit
        {
            int ex = _exit.x * tile - camPixX;
            int ey = _exit.y * tile - camPixY;
            uint16_t exitColor = _exit.unlocked
                ? (millis() % 500 < 250 ? ArcadeConfig::COLOR_GREEN : ArcadeConfig::COLOR_BLACK)
                : ArcadeConfig::COLOR_WHITE;
            canvas.drawRect(ex + 1, ey + 1, 6, 6, exitColor);
        }

        // Bombs
        for (int i = 0; i < _activeBombs; i++) {
            if (!_bombs[i].active) continue;
            int sx = _bombs[i].x * tile - camPixX;
            int sy = _bombs[i].y * tile - camPixY;
            uint16_t bColor = _bombs[i].fuseActive
                ? (millis() % 200 < 100 ? ArcadeConfig::COLOR_RED : ArcadeConfig::COLOR_YELLOW)
                : ArcadeConfig::COLOR_AMBER;
            canvas.fillCircle(sx + 4, sy + 4, 3, bColor);
            if (_bombs[i].fuseActive && _bombs[i].fuseCount > 0) {
                canvas.setTextSize(1);
                canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
                canvas.setCursor(sx + 2, sy + 1);
                canvas.print(_bombs[i].fuseCount);
            }
        }

        // Traps + bullets
        for (int i = 0; i < _activeTraps; i++) {
            int sx = _traps[i].x * tile - camPixX;
            int sy = _traps[i].y * tile - camPixY;
            uint16_t trapColor = (_traps[i].type == TrapEmitter::TYPE_A)
                ? ArcadeConfig::COLOR_AMBER : ArcadeConfig::COLOR_MAGENTA;
            canvas.fillRect(sx + 2, sy + 2, 4, 4, trapColor);
            for (const auto &b : _traps[i].bullets) {
                if (!b.active) continue;
                int bx = (int)b.x * tile - camPixX + 4;
                int by = (int)b.y * tile - camPixY + 4;
                canvas.drawPixel(bx, by, ArcadeConfig::COLOR_WHITE);
            }
        }

        // Teleports
        for (int i = 0; i < _activeTeleports; i++) {
            if (!_teleports[i].active) continue;
            int sx = _teleports[i].x * tile - camPixX;
            int sy = _teleports[i].y * tile - camPixY;
            uint16_t tpColor = millis() % 600 < 300 ? ArcadeConfig::COLOR_CYAN : ArcadeConfig::COLOR_ION_BLUE;
            canvas.drawCircle(sx + 4, sy + 4, 3, tpColor);
        }

        // Speed boosts
        for (const auto &b : _boosts) {
            if (b.collected) continue;
            int sx = b.x * tile - camPixX;
            int sy = b.y * tile - camPixY;
            canvas.drawTriangle(sx+4, sy+1, sx+1, sy+7, sx+7, sy+7, ArcadeConfig::COLOR_CYAN);
        }

        // Time bonuses
        for (const auto &b : _bonuses) {
            if (b.collected) continue;
            int sx = b.x * tile - camPixX;
            int sy = b.y * tile - camPixY;
            canvas.drawRect(sx+2, sy+1, 4, 6, ArcadeConfig::COLOR_GREEN);
        }

        // Player
        {
            int ppx = _player.x * tile - camPixX;
            int ppy = _player.y * tile - camPixY;
            canvas.fillTriangle(ppx+4, ppy+1, ppx+1, ppy+7, ppx+7, ppy+7, ArcadeConfig::COLOR_WHITE);
        }

        // HUD
        canvas.fillRect(0, 0, ArcadeConfig::PORTRAIT_WIDTH, MazeRenderer::HUD_HEIGHT, ArcadeConfig::COLOR_BLACK);
        canvas.setTextSize(1);
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.setCursor(1, 1);  canvas.print("L:"); canvas.print(_level);
        canvas.setCursor(40, 1); canvas.print("T:"); canvas.print(_timeLeft);
        canvas.setCursor(80, 1); canvas.print("S:"); canvas.print(_score);
    }

    void renderTitle(GFXcanvas16 &canvas) {
        canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
        canvas.setTextSize(2);
        canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
        canvas.setCursor(10, 40);
        canvas.print("MAZE FLUX");
        canvas.setTextSize(1);
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.setCursor(10, 80);
        canvas.print("HI: "); canvas.print(_highScore);
        if (millis() % 1000 < 600) {
            canvas.setCursor(10, 145);
            canvas.setTextColor(ArcadeConfig::COLOR_AMBER);
            canvas.print("HIT BUTTON TO START");
        }
    }

    void renderInstructions(GFXcanvas16 &canvas) {
        canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
        canvas.setTextSize(2);
        canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
        canvas.setCursor(4, 10);
        canvas.print("HOW TO PLAY");
        canvas.setTextSize(1);
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.setCursor(1, 38); canvas.print("> JOYSTICK TO MOVE");
        canvas.setCursor(1, 50); canvas.print("> FIND THE KEY");
        canvas.setCursor(1, 62); canvas.print("> REACH THE EXIT");
        canvas.setCursor(1, 74); canvas.print("> [A] ACTIVATES SWITCH");
        canvas.setCursor(1, 86); canvas.print("> AVOID BOMBS+BULLETS");
        canvas.setCursor(1, 110);
        canvas.setTextColor(ArcadeConfig::COLOR_YELLOW);
        canvas.print("HI SCORE: "); canvas.print(_highScore);
    }

    void renderGameOver(GFXcanvas16 &canvas) {
        canvas.fillScreen(ArcadeConfig::COLOR_RED);
        canvas.setTextSize(2);
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.setCursor(10, 45);
        canvas.print("GAME OVER");
        canvas.setTextSize(1);
        canvas.setCursor(19, 80);
        canvas.print("SCORE: "); canvas.print(_score);
        canvas.setCursor(7, 115);
        canvas.setTextColor(ArcadeConfig::COLOR_YELLOW);
        canvas.print("[BTN A] MAIN MENU");
    }

    void renderLevelComplete(GFXcanvas16 &canvas) {
        canvas.fillScreen(ArcadeConfig::COLOR_GREEN);
        canvas.setTextSize(2);
        canvas.setTextColor(ArcadeConfig::COLOR_BLACK);
        canvas.setCursor(10, 55);
        canvas.print("LEVEL UP!");
        canvas.setTextSize(1);
        canvas.setCursor(20, 80);
        canvas.print("SCORE: "); canvas.print(_score);
        canvas.setCursor(12, 110);
        canvas.print("[BTN A] CONTINUE");
    }

public:
    GameEngineMaze() {}

    void setTFT(Adafruit_ST7735 &tft) { _tft = &tft; }

    void init(AudioEngine &audio) {
        _prefs.begin("maze_flux", true);
        _highScore = _prefs.getInt("high_score", 0);
        _prefs.end();

        _score = 0; _level = 1; _floor = 0;
        _state        = STATE_TITLE;
        _attractTimer = millis();
        _btnAWasHeld  = true;

        initLevel();
        audio.playTone(523, 100);
    }

    bool update(GFXcanvas16 &canvas, bool btnA, bool btnB,
                bool joyUp, bool joyDown, bool joyLeft, bool joyRight,
                AudioEngine &audio) {

        // ---- TITLE ----
        if (_state == STATE_TITLE) {
            if (btnB) {
                audio.mute(); 
                return false; 
            }

            if (millis() - _attractTimer > ATTRACT_INTERVAL_MS) {
                _showInstructions = !_showInstructions;
                _attractTimer     = millis();
            }
            if (!_showInstructions) renderTitle(canvas);
            else                    renderInstructions(canvas);

            if (btnA) {
                _score = 0; _level = 1; _floor = 0;
                initLevel();
                _state = STATE_PLAYING;
            }

            if (_tft) _tft->drawRGBBitmap(0, 0, canvas.getBuffer(),
                ArcadeConfig::PORTRAIT_WIDTH, ArcadeConfig::PORTRAIT_HEIGHT);
            return true;
        }

        // ---- GAME OVER ----
        if (_state == STATE_GAMEOVER) {
            renderGameOver(canvas);
            if (_tft) _tft->drawRGBBitmap(0, 0, canvas.getBuffer(),
                ArcadeConfig::PORTRAIT_WIDTH, ArcadeConfig::PORTRAIT_HEIGHT);
            if (btnA || millis() - _gameOverMs > GAMEOVER_TIMEOUT_MS) {
                _state        = STATE_TITLE;
                _attractTimer = millis();
            }
            return true;
        }

        if (_state == STATE_LEVEL_COMPLETE) {
            renderLevelComplete(canvas);
            if (_tft) _tft->drawRGBBitmap(0, 0, canvas.getBuffer(),
                ArcadeConfig::PORTRAIT_WIDTH, ArcadeConfig::PORTRAIT_HEIGHT);
            if (btnA) {
                initLevel();
                _state = STATE_PLAYING;
            }
            return true;
        }

        // ---- PLAYING ----
        if (millis() - _lastSecondMs >= 1000UL) {
            _lastSecondMs = millis();
            _timeLeft--;
            if (_timeLeft <= 0) {
                _player.lives--;
                if (_player.lives <= 0) {
                    if (_score > _highScore) { _highScore = _score; saveHighScore(); }
                    _state      = STATE_GAMEOVER;
                    _gameOverMs = millis();
                } else {
                    _timeLeft = 60;
                }
            }
        }

        for (int i = 0; i < _activeTraps; i++) _traps[i].update();
        _particles.update();

        _player.update(joyUp, joyDown, joyLeft, joyRight, btnA, _maze);
        checkInteractions(audio);
        updateCamera();
        renderPlaying(canvas);

        if (_tft) _tft->drawRGBBitmap(0, 0, canvas.getBuffer(),
            ArcadeConfig::PORTRAIT_WIDTH, ArcadeConfig::PORTRAIT_HEIGHT);
        return true;
    }
};

#endif // GAME_ENGINE_MAZE_H