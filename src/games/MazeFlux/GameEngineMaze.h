#ifndef GAME_ENGINE_MAZE_H
#define GAME_ENGINE_MAZE_H

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Preferences.h>
#include "../../cabinet/ArcadeConfig.h"
#include "../../cabinet/AudioEngine.h"
#include "../../cabinet/ParticleManager.h"
#include "../../cabinet/HighScores.h"
#include "../../assets/shared/SharedAssets.h"
#include "MazeConfig.h"
#include "MazeGenerator.h"
#include "MazeRenderer.h"
#include "PlayerMaze.h"
#include "Obstacles.h"
#include "Collectibles.h"
#include "PlayerSprite.h"
#include "assets/TitleScreen.h"

// =============================================================================
// MAZE FLUX
// Find the keys and reach the exit before the clock runs out. A level is a
// generated maze; the exit (bottom right) opens once every key is
// collected. From level 4, doors stand across the way to the exit, each
// opened by its key, which is always somewhere before it; the last key
// opens the exit. Bombs go off if you linger near them, traps fire down
// corridors (type B's switch, A beside it, pauses it), teleport pads come
// in pairs. Tuning: MazeConfig.h.
// =============================================================================

class GameEngineMaze {
private:
    Adafruit_ST7735* _tft = nullptr;
    // The cabinet's table for this game; _highScore is its top score.
    hiscore::ScoreBoard _scores;

    MazeGenerator   _maze;
    MazeRenderer    _renderer;
    PlayerMaze      _player;
    ParticleManager _particles;

    ProximityBomb _bombs[mazecfg::MAX_BOMBS];
    TrapEmitter   _traps[mazecfg::MAX_TRAPS];
    Key           _keys[mazecfg::MAX_KEYS];
    Door          _doors[mazecfg::MAX_DOORS];
    SpeedBoost    _boosts[mazecfg::MAX_BOOSTS];
    TimeBonus     _bonuses[mazecfg::MAX_BONUSES];
    Teleport      _pads[mazecfg::MAX_PADS];
    Exit          _exit;

    int _activeBombs = 0, _activeTraps = 0, _activeKeys = 0, _activeDoors = 0, _activePads = 0;

    // Per cell: steps from the start (walls only, doors ignored) and which
    // stretch of the way it's in (how many doors lie between it and the
    // start).
    static const int MAX_CELLS = MazeGenerator::MAX_W * MazeGenerator::MAX_H;
    uint16_t _dist[MAX_CELLS];
    uint8_t  _region[MAX_CELLS];
    uint8_t  _visited[MAX_CELLS];       // breadcrumbs: cells walked through
    uint8_t  _seen[MAX_CELLS];          // cells that have been on screen (the map shows these)
    bool     _onPath[MAX_CELLS];        // the way to the exit, before the loops

    // NAME: entering a name for the high-score table, after the last life.
    // PICK: the stage select.
    enum GameState { STATE_TITLE, STATE_PLAYING, STATE_NAME, STATE_GAMEOVER, STATE_LEVEL_COMPLETE, STATE_PICK };
    GameState _state = STATE_TITLE;

    int  _level     = 1;
    int  _score     = 0;
    int  _highScore = 0;
    int  _levelTime = 0;     // seconds the level starts with
    int  _timeLeft  = 0;

    unsigned long _lastSecondMs = 0;
    unsigned long _lastUpdateMs = 0;
    unsigned long _frameMs      = 0;     // the last frame's length
    unsigned long _attractTimer = 0;
    unsigned long _gameOverMs   = 0;

    // Losing a life: the pause (_dyingUntil), then safe until _safeUntil.
    enum DeathCause { DEATH_BOMB, DEATH_BULLET, DEATH_TIME };
    unsigned long _dyingUntil = 0;
    unsigned long _safeUntil  = 0;
    DeathCause    _deathCause = DEATH_BULLET;

    static const unsigned long ATTRACT_INTERVAL_MS = 8000UL;
    static const unsigned long GAMEOVER_TIMEOUT_MS = 30000UL;

    bool _btnAWasHeld   = false;
    bool _prevBtnA      = false;
    // Game over and level complete: A counts only once it has been up
    // after GAMEOVER_INPUT_DELAY_MS.
    bool _endInputArmed = false;
    int  _attractPage   = 0;           // title, how to play, high scores

    int   _camX = 0, _camY = 0;        // the view's top left, maze pixels
    float _camFX = 0, _camFY = 0;      // the same, easing

    // Which optional sounds are on the card (checked once, in init()).
    bool _pickupOnCard = false, _powerupOnCard = false;
    bool _doorOnCard = false, _switchOnCard = false, _teleportOnCard = false;
    static constexpr const char* PICKUP_WAV   = "/audio/pickup.wav";
    static constexpr const char* POWERUP_WAV  = "/audio/powerup.wav";
    static constexpr const char* DOOR_WAV     = "/audio/maze_door.wav";
    static constexpr const char* SWITCH_WAV   = "/audio/maze_switch.wav";
    static constexpr const char* TELEPORT_WAV = "/audio/maze_teleport.wav";

    // The attract demo: the autopilot plays a random level, silently, for
    // DEMO_MIN_MS to DEMO_MAX_MS (or until its lives run low).
    bool _demo = false;
    unsigned long _demoUntil = 0;
    bool _apA = false;                     // the autopilot's A, toggled to make presses
    static const unsigned long DEMO_MIN_MS = 30000, DEMO_MAX_MS = 40000;
    static const int DEMO_MIN_LEVEL = 2, DEMO_MAX_LEVEL = 8;

    // The stage select (B held with A on the title): a test run from any
    // level, which puts nothing on the table.
    bool _test = false;
    int  _testFrom = 1;
    int  _pick = 1, _pickDir = 0;
    unsigned long _pickRepeatAt = 0, _pickAt = 0;
    static const int PICK_LEVELS = 30;
    static const unsigned long PICK_TIMEOUT_MS = 20000, PICK_REPEAT_DELAY_MS = 400, PICK_REPEAT_MS = 150;

    // The map: B held in play. Everything stops but the clock.
    bool _mapOpen = false;

    // Out of lives: a name for the table first, if the score made it.
    void endGame(AudioEngine &audio) {
        _scores.forget();
        _state      = !_test && _scores.offer(_score) ? STATE_NAME : STATE_GAMEOVER;
        _gameOverMs = millis();
        _endInputArmed = false;
        audio.stopLoop();
    }

    // -------------------------------------------------------------------------
    // The maze's ways
    // -------------------------------------------------------------------------
    int cellIndex(int x, int y) const { return y * _maze.width + x; }

    // The door across the way out of (x, y) towards dir, or -1.
    int doorAt(int x, int y, uint8_t dir) const {
        const int nx = x + MazeGenerator::dx(dir), ny = y + MazeGenerator::dy(dir);
        const uint8_t opp = MazeGenerator::opposite(dir);
        for (int i = 0; i < _activeDoors; i++) {
            const Door &d = _doors[i];
            if ((d.x == x && d.y == y && d.dir == dir) || (d.x == nx && d.y == ny && d.dir == opp)) return i;
        }
        return -1;
    }

    // Whether the way out of (x, y) towards dir is open: no wall, no closed
    // door, and still inside the maze.
    bool canPass(int x, int y, uint8_t dir) const {
        if (!_maze.inside(x, y) || _maze.hasWall(x, y, dir)) return false;
        if (!_maze.inside(x + MazeGenerator::dx(dir), y + MazeGenerator::dy(dir))) return false;
        const int d = doorAt(x, y, dir);
        return d < 0 || _doors[d].open;
    }

    // Steps from (ax, ay) to (bx, by) through open ways, if no more than
    // limit; else limit + 1.
    int stepsWithin(int ax, int ay, int bx, int by, int limit) const {
        if (ax == bx && ay == by) return 0;
        if (abs(ax - bx) + abs(ay - by) > limit) return limit + 1;
        static int16_t qx[64], qy[64];
        static uint8_t qd[64];
        int head = 0, tail = 0;
        qx[tail] = ax; qy[tail] = ay; qd[tail] = 0; tail++;
        while (head < tail) {
            const int x = qx[head], y = qy[head], d = qd[head]; head++;
            if (d >= limit) continue;
            for (uint8_t dir : MazeGenerator::DIRS) {
                if (!canPass(x, y, dir)) continue;
                const int nx = x + MazeGenerator::dx(dir), ny = y + MazeGenerator::dy(dir);
                if (nx == bx && ny == by) return d + 1;
                bool seen = false;
                for (int k = 0; k < tail; k++) if (qx[k] == nx && qy[k] == ny) { seen = true; break; }
                if (!seen && tail < 64) { qx[tail] = nx; qy[tail] = ny; qd[tail] = d + 1; tail++; }
            }
        }
        return limit + 1;
    }

    // -------------------------------------------------------------------------
    // Level setup
    // -------------------------------------------------------------------------
    // Steps from the start to every cell, through the walls only.
    void measureFromStart() {
        static int16_t q[MAX_CELLS];
        const int n = _maze.width * _maze.height;
        for (int i = 0; i < n; i++) _dist[i] = 0xFFFF;
        int head = 0, tail = 0;
        q[tail++] = 0; _dist[0] = 0;
        while (head < tail) {
            const int c = q[head++], x = c % _maze.width, y = c / _maze.width;
            for (uint8_t dir : MazeGenerator::DIRS) {
                if (_maze.hasWall(x, y, dir)) continue;
                const int nx = x + MazeGenerator::dx(dir), ny = y + MazeGenerator::dy(dir);
                if (!_maze.inside(nx, ny)) continue;
                const int nc = cellIndex(nx, ny);
                if (_dist[nc] != 0xFFFF) continue;
                _dist[nc] = _dist[c] + 1;
                q[tail++] = (int16_t)nc;
            }
        }
    }

    // The stretch each cell is in: 0 before the first door, 1 between the
    // first and second, and so on.
    void measureRegions() {
        static int16_t q[MAX_CELLS];
        const int n = _maze.width * _maze.height;
        for (int i = 0; i < n; i++) _region[i] = 0xFF;
        for (int r = 0; r <= _activeDoors; r++) {
            int head = 0, tail = 0;
            if (r == 0) { q[tail++] = 0; _region[0] = 0; }
            else {
                // Beyond door r-1: its far side.
                const Door &d = _doors[r - 1];
                const int nx = d.x + MazeGenerator::dx(d.dir), ny = d.y + MazeGenerator::dy(d.dir);
                const int c = _region[cellIndex(d.x, d.y)] == r - 1 ? cellIndex(nx, ny) : cellIndex(d.x, d.y);
                if (_region[c] != 0xFF) continue;
                _region[c] = r; q[tail++] = (int16_t)c;
            }
            while (head < tail) {
                const int c = q[head++], x = c % _maze.width, y = c / _maze.width;
                for (uint8_t dir : MazeGenerator::DIRS) {
                    if (_maze.hasWall(x, y, dir) || doorAt(x, y, dir) >= 0) continue;
                    const int nx = x + MazeGenerator::dx(dir), ny = y + MazeGenerator::dy(dir);
                    if (!_maze.inside(nx, ny)) continue;
                    const int nc = cellIndex(nx, ny);
                    if (_region[nc] != 0xFF) continue;
                    _region[nc] = r;
                    q[tail++] = (int16_t)nc;
                }
            }
        }
    }

    // Whether anything already stands on (x, y), or it's the start's
    // surroundings or the exit.
    bool isFree(int x, int y) const {
        if (!_maze.inside(x, y)) return false;
        if (_dist[cellIndex(x, y)] < mazecfg::START_CLEAR) return false;
        if (x == _exit.x && y == _exit.y) return false;
        for (int i = 0; i < _activeKeys;  i++) if (_keys[i].x  == x && _keys[i].y  == y) return false;
        for (int i = 0; i < _activeBombs; i++) if (_bombs[i].x == x && _bombs[i].y == y) return false;
        for (int i = 0; i < _activeTraps; i++) if (_traps[i].x == x && _traps[i].y == y) return false;
        for (int i = 0; i < _activePads;  i++) if (_pads[i].x  == x && _pads[i].y  == y) return false;
        for (const auto &b : _boosts)  if (b.active && b.x == x && b.y == y) return false;
        for (const auto &b : _bonuses) if (b.active && b.x == x && b.y == y) return false;
        return true;
    }

    // A random free cell (optionally in a given stretch, at least minDist
    // from the start); false if none turned up.
    bool randomFree(int &x, int &y, int region = -1, int minDist = 0) {
        for (int attempt = 0; attempt < 200; attempt++) {
            x = random(0, _maze.width);
            y = random(0, _maze.height);
            const int c = cellIndex(x, y);
            if (region >= 0 && _region[c] != region) continue;
            if (_dist[c] < minDist) continue;
            if (isFree(x, y)) return true;
        }
        return false;
    }

    void initLevel() {
        using namespace mazecfg;
        _maze.generate(min(mazeWidthFor(_level), (int)MazeGenerator::MAX_W),
                       min(mazeHeightFor(_level), (int)MazeGenerator::MAX_H));
        const int w = _maze.width, h = _maze.height;
        memset(_visited, 0, sizeof(_visited));
        memset(_seen, 0, sizeof(_seen));
        _visited[0] = 1;
        _mapOpen = false;

        _player.reset(0, 0);
        _particles.clearAll();
        _activeBombs = _activeTraps = _activeKeys = _activeDoors = _activePads = 0;
        for (auto &b : _boosts)  b.active = false;
        for (auto &b : _bonuses) b.active = false;
        _exit = { _maze.width - 1, _maze.height - 1, false };
        _dyingUntil = 0;
        _safeUntil  = 0;

        measureFromStart();
        placeDoors();
        braid();
        measureFromStart();
        placeKeys();
        placeBombs();
        placeTraps();
        placePads();
        placePickups();

        _levelTime    = (int)(TIME_BASE_S + TIME_PER_CELL_S * w * h);
        _timeLeft     = _levelTime;
        _lastSecondMs = _lastUpdateMs = millis();
        updateCamera(0, true);
    }

    // Doors across the way from the start to the exit (the maze is still
    // perfect: one way), spaced along it; each stretch between them is
    // measured. Marks the way in _onPath.
    void placeDoors() {
        using namespace mazecfg;
        // The way, from the exit back to the start (the maze is a tree).
        static int16_t path[MAX_CELLS];
        int len = 0;
        int x = _exit.x, y = _exit.y;
        path[len++] = (int16_t)cellIndex(x, y);
        while (x || y) {
            const int d = _dist[cellIndex(x, y)];
            for (uint8_t dir : MazeGenerator::DIRS) {
                if (_maze.hasWall(x, y, dir)) continue;
                const int nx = x + MazeGenerator::dx(dir), ny = y + MazeGenerator::dy(dir);
                if (_maze.inside(nx, ny) && _dist[cellIndex(nx, ny)] == d - 1) { x = nx; y = ny; break; }
            }
            path[len++] = (int16_t)cellIndex(x, y);
        }
        memset(_onPath, 0, sizeof(_onPath));
        for (int i = 0; i < len; i++) _onPath[path[i]] = true;

        // Doors at even spacing from the start (path is exit-first), never
        // inside the start's surroundings.
        int doors = doorsFor(_level);
        while (doors > 0 && (len - 1) / (doors + 1) < 4) doors--;
        for (int i = 0; i < doors; i++) {
            const int stepsIn = (len - 1) * (i + 1) / (doors + 1);   // from the start
            const int near = path[len - 1 - stepsIn], far = path[len - 2 - stepsIn];
            const int nx = near % _maze.width, ny = near / _maze.width;
            const int fx = far % _maze.width, fy = far / _maze.width;
            const uint8_t dir = fx > nx ? WALL_E : fx < nx ? WALL_W : fy > ny ? WALL_S : WALL_N;
            _doors[_activeDoors++] = { nx, ny, dir, (uint8_t)i, false };
        }
        measureRegions();
    }

    // Loops: take a wall out of BRAID_PERCENT of the dead ends, into a
    // neighbour in the same stretch (so every door is still the only way
    // on), a dead-end neighbour first.
    void braid() {
        using namespace mazecfg;
        for (int y = 0; y < _maze.height; y++) {
            for (int x = 0; x < _maze.width; x++) {
                if (exits(x, y) != 1 || random(0, 100) >= BRAID_PERCENT) continue;
                const int r = _region[cellIndex(x, y)];
                uint8_t pick = 0;
                for (int k = 0, start = random(0, 4); k < 4; k++) {
                    const uint8_t dir = MazeGenerator::DIRS[(start + k) & 3];
                    if (!_maze.hasWall(x, y, dir)) continue;
                    const int nx = x + MazeGenerator::dx(dir), ny = y + MazeGenerator::dy(dir);
                    if (!_maze.inside(nx, ny) || _region[cellIndex(nx, ny)] != r) continue;
                    if (!pick || exits(nx, ny) == 1) pick = dir;
                    if (exits(nx, ny) == 1) break;
                }
                if (pick) _maze.open(x, y, pick);
            }
        }
    }

    // A key per stretch, off the way to the exit if possible (fetching one
    // is a detour); the last, beyond the last door, opens the exit.
    void placeKeys() {
        using namespace mazecfg;
        for (int k = 0; k <= _activeDoors; k++) {
            int kx = 0, ky = 0;
            bool found = false;
            for (int attempt = 0; attempt < 300 && !found; attempt++) {
                if (!randomFree(kx, ky, k, START_CLEAR)) break;
                found = !_onPath[cellIndex(kx, ky)] || attempt > 150;
            }
            if (!found) { found = randomFree(kx, ky, k, 1); }
            if (!found) { kx = _exit.x; ky = _exit.y; }   // never: a stretch is a few cells at least
            _keys[_activeKeys++] = { kx, ky, (uint8_t)k, false };
        }
    }

    void placeBombs() {
        using namespace mazecfg;
        const bool hasBombs = (_level >= 3 && _level <= 4) || (_level >= 7 && _level != 9 && _level != 10);
        if (!hasBombs) return;
        const int count = constrain(_level / 3, 1, MAX_BOMBS);
        for (int i = 0; i < count; i++) {
            int x, y;
            if (!randomFree(x, y, -1, START_CLEAR + BOMB_RANGE)) continue;
            _bombs[_activeBombs++].init(x, y);
        }
    }

    // Straight open cells in front of (x, y) towards dir, through walls
    // only (doors open later); -1 if the line of fire passes through the
    // start's surroundings.
    int laneFrom(int x, int y, uint8_t dir) const {
        int n = 0;
        while (!_maze.hasWall(x, y, dir)) {
            x += MazeGenerator::dx(dir); y += MazeGenerator::dy(dir);
            if (!_maze.inside(x, y)) break;
            if (_dist[cellIndex(x, y)] < mazecfg::START_CLEAR) return -1;
            n++;
        }
        return n;
    }

    void placeTraps() {
        using namespace mazecfg;
        const int typeA = (_level >= 5 && _level <= 8) ? 1 + (_level >= 7) : _level >= 11 ? 2 : 0;
        const int typeB = _level >= 13 ? 2 : _level >= 9 ? 1 : 0;
        for (int i = 0; i < typeA + typeB && _activeTraps < MAX_TRAPS; i++) {
            for (int attempt = 0; attempt < 60; attempt++) {
                int x, y;
                if (!randomFree(x, y, -1, START_CLEAR)) break;
                const uint8_t dir = MazeGenerator::DIRS[random(0, 4)];
                const int lane = laneFrom(x, y, dir);
                if (lane < TRAP_MIN_LANE) continue;
                _traps[_activeTraps++].init(x, y, MazeGenerator::dx(dir), MazeGenerator::dy(dir),
                                            i < typeA ? TrapEmitter::TYPE_A : TrapEmitter::TYPE_B, lane);
                break;
            }
        }
    }

    // Open ways out of a cell (walls only).
    int exits(int x, int y) const {
        int n = 0;
        for (uint8_t dir : MazeGenerator::DIRS) n += !_maze.hasWall(x, y, dir);
        return n;
    }

    // Pairs of pads in the same stretch (so they never skip a door), some
    // way apart, each in a dead end: a pad on a way through would send you
    // back every time you tried to pass it.
    void placePads() {
        using namespace mazecfg;
        for (int p = 0; p < teleportPairsFor(_level); p++) {
            for (int attempt = 0; attempt < 120; attempt++) {
                int ax, ay, bx, by;
                if (!randomFree(ax, ay, -1, START_CLEAR) || exits(ax, ay) != 1) continue;
                const int r = _region[cellIndex(ax, ay)];
                _pads[_activePads] = { ax, ay, _activePads + 1 };
                _activePads++;                       // so isFree() sees it
                bool ok = false;
                for (int k = 0; k < 60 && !ok; k++)
                    ok = randomFree(bx, by, r, START_CLEAR) && exits(bx, by) == 1 && abs(ax - bx) + abs(ay - by) >= PAD_MIN_APART;
                _activePads--;
                if (!ok) continue;
                _pads[_activePads]     = { ax, ay, _activePads + 1 };
                _pads[_activePads + 1] = { bx, by, _activePads };
                _activePads += 2;
                break;
            }
        }
    }

    void placePickups() {
        using namespace mazecfg;
        for (auto &b : _boosts)  { int x, y; if (randomFree(x, y, -1, START_CLEAR)) b = { x, y, true }; }
        for (auto &b : _bonuses) { int x, y; if (randomFree(x, y, -1, START_CLEAR)) b = { x, y, true }; }
    }

    // -------------------------------------------------------------------------
    // Camera: the player centred (a little ahead while moving), within the
    // maze and its outer wall, easing there. snap: straight there.
    // -------------------------------------------------------------------------
    void updateCamera(unsigned long dtMs, bool snap = false) {
        using namespace mazecfg;
        const int edge = WALL_T / 2;
        const int mw = _maze.width * CELL + 2 * edge, mh = _maze.height * CELL + 2 * edge;
        float tx = _player.px() - VIEW_W / 2.0f, ty = _player.py() - VIEW_H / 2.0f;
        if (_player.moving) {
            tx += (_player.x - _player.fromX) * LOOK_AHEAD;
            ty += (_player.y - _player.fromY) * LOOK_AHEAD;
        }
        tx = mw <= VIEW_W ? (mw - VIEW_W) / 2.0f - edge : constrain(tx, (float)-edge, (float)(mw - VIEW_W - edge));
        ty = mh <= VIEW_H ? (mh - VIEW_H) / 2.0f - edge : constrain(ty, (float)-edge, (float)(mh - VIEW_H - edge));
        const float k = snap ? 1.0f : min(1.0f, dtMs / CAMERA_EASE_MS);
        _camFX += (tx - _camFX) * k;
        _camFY += (ty - _camFY) * k;
        _camX = (int)lroundf(_camFX);
        _camY = (int)lroundf(_camFY);
    }

    // -------------------------------------------------------------------------
    // Play
    // -------------------------------------------------------------------------
    bool safe() const { return millis() < _safeUntil; }

    void sfxPickup(AudioEngine &audio) {
        if (_pickupOnCard) audio.playWAV(PICKUP_WAV);
        else audio.playPowerUpExtraLife();
    }
    void sfxDoor(AudioEngine &audio) {
        if (_doorOnCard) audio.playWAV(DOOR_WAV);
        else audio.playTone(262, 160);
    }
    void sfxSwitch(AudioEngine &audio) {
        if (_switchOnCard) audio.playWAV(SWITCH_WAV);
        else audio.playTone(523, 80);
    }
    void sfxTeleport(AudioEngine &audio) {
        if (_teleportOnCard) audio.playWAV(TELEPORT_WAV);
        else audio.playTone(440, 150);
    }
    void sfxPowerup(AudioEngine &audio, int freq) {
        if (_powerupOnCard) audio.playWAV(POWERUP_WAV);
        else audio.playTone(freq, 100);
    }

    // A life lost: the pause first (finishDying() after).
    void die(DeathCause cause, AudioEngine &audio) {
        if (_dyingUntil) return;
        _deathCause = cause;
        _dyingUntil = millis() + mazecfg::DEATH_MS;
        const float x = _player.px(), y = _player.py();
        _particles.spawnExplosion(x, y, ArcadeConfig::COLOR_CYAN, 18, 700, 4);
        _particles.spawnExplosion(x, y, ArcadeConfig::COLOR_WHITE, 8, 500, 3);
        if (cause == DEATH_BOMB) audio.playExplosionSound(explosion_data, sizeof(explosion_data));
        else audio.playDeathSound();
    }

    void finishDying(AudioEngine &audio) {
        _dyingUntil = 0;
        if (_demo && _player.lives <= 1) { endDemo(); return; }   // a demo never reaches game over
        if (--_player.lives <= 0) { endGame(audio); return; }
        _player.reset(0, 0);
        updateCamera(0, true);
        _safeUntil = millis() + mazecfg::RESPAWN_SAFE_MS;
        for (int i = 0; i < _activeTraps; i++) _traps[i].clearBullets();
        for (int i = 0; i < _activeBombs; i++) _bombs[i].disarm();
        if (_deathCause == DEATH_TIME) _timeLeft = _levelTime;
        _lastSecondMs = millis();
    }

    void completeLevel(AudioEngine &audio) {
        using namespace mazecfg;
        if (_demo) { _level++; initLevel(); return; }       // a demo just goes on to the next
        _score += _timeLeft * PTS_PER_SECOND + _level * PTS_LEVEL;
        _level++;
        _state = STATE_LEVEL_COMPLETE;
        _gameOverMs = millis();
        _endInputArmed = false;
        audio.playLandingSuccessSound();
    }

    // What's on the cell just reached.
    void arriveAt(int x, int y, AudioEngine &audio) {
        using namespace mazecfg;
        for (int i = 0; i < _activeKeys; i++) {
            Key &k = _keys[i];
            if (k.collected || k.x != x || k.y != y) continue;
            k.collected = true;
            _score += PTS_KEY;
            bool opened = false;
            for (int d = 0; d < _activeDoors; d++)
                if (_doors[d].colourId == k.colourId && !_doors[d].open) { _doors[d].open = true; opened = true; }
            if (opened) sfxDoor(audio);      // the key, and its door unlocking somewhere
            else sfxPickup(audio);
            bool all = true;
            for (int j = 0; j < _activeKeys; j++) all &= _keys[j].collected;
            _exit.unlocked = all;
        }
        for (auto &b : _boosts) {
            if (!b.active || b.x != x || b.y != y) continue;
            b.active = false;
            _score += PTS_PICKUP;
            _player.applySpeedBoost();
            sfxPowerup(audio, 880);
        }
        for (auto &b : _bonuses) {
            if (!b.active || b.x != x || b.y != y) continue;
            b.active = false;
            _score += PTS_PICKUP;
            _timeLeft += TIME_BONUS_S;
            sfxPowerup(audio, 660);
        }
        for (int i = 0; i < _activePads; i++) {
            if (_pads[i].x != x || _pads[i].y != y) continue;
            const Teleport &to = _pads[_pads[i].partner];
            _player.reset(to.x, to.y);       // standing on the partner: no hop back till you step off and on
            sfxTeleport(audio);
            break;
        }
        if (_exit.unlocked && x == _exit.x && y == _exit.y) completeLevel(audio);
    }

    void updatePlaying(bool up, bool down, bool left, bool right, bool aPressed, AudioEngine &audio) {
        using namespace mazecfg;
        const unsigned long now = millis();
        const unsigned long dt = min(now - _lastUpdateMs, 100UL);
        _lastUpdateMs = now;
        _frameMs = dt;

        if (_dyingUntil) {
            if (now >= _dyingUntil) finishDying(audio);
            return;
        }

        if (now - _lastSecondMs >= 1000UL) {
            _lastSecondMs += 1000UL;
            if (--_timeLeft <= 0) { _timeLeft = 0; die(DEATH_TIME, audio); return; }
        }

        // With the map up, you stand where you are (the rest goes on).
        if (_mapOpen) up = down = left = right = false;
        _player.update(up, down, left, right,
                       [this](int x, int y, uint8_t dir) { return canPass(x, y, dir); });
        // The cell just reached, not where the player's already heading
        // on (held, the next step starts the same update).
        if (_player.arrived) {
            _visited[cellIndex(_player.arrivedX, _player.arrivedY)] = 1;
            arriveAt(_player.arrivedX, _player.arrivedY, audio);
            if (!_player.moving) _visited[cellIndex(_player.x, _player.y)] = 1;   // where a pad put you
        }
        if (_state != STATE_PLAYING) return;

        // A in or beside a type B trap's line of fire: its switch.
        if (aPressed) {
            for (int i = 0; i < _activeTraps; i++) {
                TrapEmitter &t = _traps[i];
                if (t.type != TrapEmitter::TYPE_B) continue;
                if (t.switchReach(_player.nearX(), _player.nearY())) {
                    t.activateSwitch();
                    sfxSwitch(audio);
                }
            }
        }

        for (int i = 0; i < _activeBombs; i++) {
            ProximityBomb &b = _bombs[i];
            if (!b.active) continue;
            const bool inReach = stepsWithin(_player.nearX(), _player.nearY(), b.x, b.y, BOMB_RANGE) <= BOMB_RANGE;
            if (!b.update(inReach)) continue;
            _particles.spawnExplosion((b.x + 0.5f) * CELL, (b.y + 0.5f) * CELL, ArcadeConfig::COLOR_AMBER, 16, 600, 4);
            _particles.spawnExplosion((b.x + 0.5f) * CELL, (b.y + 0.5f) * CELL, ArcadeConfig::COLOR_RED, 10, 500, 3);
            if (inReach && !safe()) { die(DEATH_BOMB, audio); return; }
            audio.playExplosionSound(explosion_data, sizeof(explosion_data));
        }

        const float px = _player.px(), py = _player.py(), reach = CELL * 0.45f;
        for (int i = 0; i < _activeTraps; i++) {
            TrapEmitter &t = _traps[i];
            t.update(dt, [this](int x, int y, uint8_t dir) { return canPass(x, y, dir); });
            if (safe()) continue;
            for (auto &b : t.bullets) {
                if (!b.active || fabsf(b.x - px) > reach || fabsf(b.y - py) > reach) continue;
                b.active = false;
                die(DEATH_BULLET, audio);
                return;
            }
        }
    }

    // -------------------------------------------------------------------------
    // Render
    // -------------------------------------------------------------------------
    static uint16_t keyColour(uint8_t id) {
        static const uint16_t C[mazecfg::MAX_KEYS] = {
            ArcadeConfig::COLOR_YELLOW, ArcadeConfig::COLOR_MAGENTA, ArcadeConfig::COLOR_GREEN, ArcadeConfig::COLOR_CYAN };
        return C[id % mazecfg::MAX_KEYS];
    }

    // Maze pixels to the screen.
    int sx(float mx) const { return (int)mx - _camX; }
    int sy(float my) const { return (int)my - _camY + mazecfg::HUD_H; }

    // -------------------------------------------------------------------------
    // Autopilot (the attract demo, and the harness's bot): the shortest open
    // way to the next key (lowest colour first), then the exit, never
    // through a teleport pad it isn't heading for. At a trap's line of
    // fire it waits for a gap (type A: no bullet in the air and time to
    // walk the lane before the next) or uses the switch (type B). Returns
    // its stick as screen directions in `dir` (a WALL_ bit, 0 to stand),
    // and whether to press A.
    // -------------------------------------------------------------------------
    void autopilot(uint8_t &dir, bool &pressA) {
        using namespace mazecfg;
        dir = 0; pressA = false;
        const int W = _maze.width, H = _maze.height;
        int tx = _exit.x, ty = _exit.y;
        for (int k = 0; k < _activeKeys; k++) if (!_keys[k].collected) { tx = _keys[k].x; ty = _keys[k].y; break; }
        static int16_t prev[MAX_CELLS], q[MAX_CELLS];
        for (int i = 0; i < W * H; i++) prev[i] = -2;
        const int start = cellIndex(_player.x, _player.y), goal = cellIndex(tx, ty);
        if (start == goal) return;
        int head = 0, tail = 0;
        q[tail++] = (int16_t)start; prev[start] = -1;
        while (head < tail && prev[goal] == -2) {
            const int c = q[head++], x = c % W, y = c / W;
            for (uint8_t d : MazeGenerator::DIRS) {
                if (!canPass(x, y, d)) continue;
                const int nc = cellIndex(x + MazeGenerator::dx(d), y + MazeGenerator::dy(d));
                if (prev[nc] != -2) continue;
                bool pad = false;
                for (int i = 0; i < _activePads; i++) pad |= cellIndex(_pads[i].x, _pads[i].y) == nc;
                if (pad && nc != goal) continue;
                prev[nc] = (int16_t)c;
                q[tail++] = (int16_t)nc;
            }
        }
        (void)H;
        if (prev[goal] == -2) return;
        int c = goal;
        while (prev[c] != start) c = prev[c];
        const int nx = c % W, ny = c / W;
        // Into a trap's lane only when it's safe (mid-step too: the held
        // stick would carry it on).
        for (int i = 0; i < _activeTraps; i++) {
            const TrapEmitter &t = _traps[i];
            if (!t.active || !t.inLane(nx, ny) || t.inLane(_player.x, _player.y)) continue;
            const bool wait = t.type == TrapEmitter::TYPE_B
                ? !t.paused()
                : t.bulletsInFlight() || t.msToNextShot() < (unsigned long)(t.laneLen + 2) * MOVE_MS;
            if (!wait) continue;
            if (t.type == TrapEmitter::TYPE_B && t.switchReach(_player.x, _player.y)) {
                _apA = !_apA;
                pressA = _apA;
            }
            return;
        }
        dir = nx > _player.x ? WALL_E : nx < _player.x ? WALL_W : ny > _player.y ? WALL_S : WALL_N;
    }

    // Cells in view are seen: the map shows them.
    void markSeen() {
        using namespace mazecfg;
        const int x0 = max(0, _camX / CELL), y0 = max(0, _camY / CELL);
        const int x1 = min(_maze.width - 1, (_camX + VIEW_W - 1) / CELL), y1 = min(_maze.height - 1, (_camY + VIEW_H - 1) / CELL);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) _seen[cellIndex(x, y)] = 1;
    }

    // The map: the whole maze, as much of it as has been seen, scaled to
    // the screen under the HUD (up to 8px a cell); walked cells tinted,
    // keys, shut doors and the exit where seen, you blinking.
    void renderMap(GFXcanvas16 &canvas, bool everything = false) {
        using namespace mazecfg;
        const MazeTheme &theme = MazeRenderer::themeFor(_level);
        const int s = max(3, min(8, min((VIEW_W - 4) / _maze.width, (VIEW_H - 4) / _maze.height)));
        const int ox = (VIEW_W - s * _maze.width) / 2, oy = HUD_H + (VIEW_H - s * _maze.height) / 2;
        canvas.fillRect(0, HUD_H, VIEW_W, VIEW_H, ArcadeConfig::COLOR_BLACK);
        const uint16_t crumb = MazeRenderer::mix(theme.floor, theme.wall, 72);
        auto seen = [&](int x, int y) { return everything || _seen[cellIndex(x, y)]; };
        for (int y = 0; y < _maze.height; y++)
            for (int x = 0; x < _maze.width; x++) {
                if (!seen(x, y)) continue;
                const int px = ox + x * s, py = oy + y * s;
                canvas.fillRect(px, py, s, s, _visited[cellIndex(x, y)] ? crumb : theme.floor);
            }
        for (int y = 0; y < _maze.height; y++)
            for (int x = 0; x < _maze.width; x++) {
                if (!seen(x, y)) continue;
                const int px = ox + x * s, py = oy + y * s;
                if (_maze.hasWall(x, y, WALL_N)) canvas.drawFastHLine(px, py, s + 1, theme.light);
                if (_maze.hasWall(x, y, WALL_W)) canvas.drawFastVLine(px, py, s + 1, theme.light);
                if (_maze.hasWall(x, y, WALL_S)) canvas.drawFastHLine(px, py + s, s + 1, theme.light);
                if (_maze.hasWall(x, y, WALL_E)) canvas.drawFastVLine(px + s, py, s + 1, theme.light);
            }
        for (int i = 0; i < _activeDoors; i++) {
            const Door &d = _doors[i];
            if (d.open || !seen(d.x, d.y)) continue;
            const int px = ox + d.x * s, py = oy + d.y * s;
            const uint16_t col = keyColour(d.colourId);
            if (d.dir == WALL_E)      canvas.drawFastVLine(px + s, py, s + 1, col);
            else if (d.dir == WALL_W) canvas.drawFastVLine(px, py, s + 1, col);
            else if (d.dir == WALL_S) canvas.drawFastHLine(px, py + s, s + 1, col);
            else                      canvas.drawFastHLine(px, py, s + 1, col);
        }
        const int dot = max(1, s / 2), in = (s - dot + 1) / 2;
        for (int i = 0; i < _activeKeys; i++)
            if (!_keys[i].collected && seen(_keys[i].x, _keys[i].y))
                canvas.fillRect(ox + _keys[i].x * s + in, oy + _keys[i].y * s + in, dot, dot, keyColour(_keys[i].colourId));
        if (seen(_exit.x, _exit.y))
            canvas.fillRect(ox + _exit.x * s + in, oy + _exit.y * s + in, dot, dot,
                            _exit.unlocked ? ArcadeConfig::COLOR_GREEN : ArcadeConfig::COLOR_GREY);
        if (millis() % 400 < 250)
            canvas.fillRect(ox + _player.nearX() * s + in, oy + _player.nearY() * s + in, dot, dot, ArcadeConfig::COLOR_WHITE);
    }

    // The compass: the next thing to fetch (the lowest key not yet
    // collected, else the exit) and, if it's off screen, where the arrow
    // goes: on the view's edge, inset COMPASS_INSET, the way to it from
    // the player (as the crow flies). False while it's in view.
    void compassTarget(int &tx, int &ty, uint16_t &col) const {
        for (int k = 0; k < _activeKeys; k++)
            if (!_keys[k].collected) { tx = _keys[k].x; ty = _keys[k].y; col = keyColour(_keys[k].colourId); return; }
        tx = _exit.x; ty = _exit.y; col = ArcadeConfig::COLOR_GREEN;
    }
    static constexpr int COMPASS_INSET = 7;
    bool compassArrow(int &ax, int &ay, float &dx, float &dy, uint16_t &col) const {
        using namespace mazecfg;
        int tx, ty;
        compassTarget(tx, ty, col);
        const float px = sx(_player.px()), py = sy(_player.py());
        const float qx = cx(tx), qy = cy(ty);
        const float L = COMPASS_INSET, R = ArcadeConfig::PORTRAIT_WIDTH - 1 - COMPASS_INSET;
        const float T = HUD_H + COMPASS_INSET, B = ArcadeConfig::PORTRAIT_HEIGHT - 1 - COMPASS_INSET;
        if (qx >= L - CELL / 2 && qx <= R + CELL / 2 && qy >= T - CELL / 2 && qy <= B + CELL / 2) return false;
        dx = qx - px; dy = qy - py;
        const float len = sqrtf(dx * dx + dy * dy);
        if (len < 1.0f) return false;
        dx /= len; dy /= len;
        float t = 1e9f;
        if (dx > 0.001f)  t = min(t, (R - px) / dx);
        if (dx < -0.001f) t = min(t, (L - px) / dx);
        if (dy > 0.001f)  t = min(t, (B - py) / dy);
        if (dy < -0.001f) t = min(t, (T - py) / dy);
        ax = (int)lroundf(px + dx * t);
        ay = (int)lroundf(py + dy * t);
        return true;
    }

    // Centre of a cell on screen.
    int cx(int cellX) const { return sx(cellX * mazecfg::CELL + mazecfg::CELL / 2); }
    int cy(int cellY) const { return sy(cellY * mazecfg::CELL + mazecfg::CELL / 2); }
    bool onScreen(int x, int y, int margin = mazecfg::CELL) const {
        return x > -margin && x < ArcadeConfig::PORTRAIT_WIDTH + margin && y > mazecfg::HUD_H - margin &&
               y < ArcadeConfig::PORTRAIT_HEIGHT + margin;
    }

    static void drawKey(GFXcanvas16 &c, int x, int y, uint16_t col) {
        c.fillCircle(x - 3, y, 3, col);
        c.drawPixel(x - 3, y, ArcadeConfig::COLOR_BLACK);
        c.drawFastHLine(x, y, 6, col);
        c.drawFastHLine(x, y + 1, 6, col);
        c.drawFastVLine(x + 3, y + 2, 2, col);
        c.drawFastVLine(x + 5, y + 2, 3, col);
    }

    void renderPlaying(GFXcanvas16 &canvas) {
        using namespace mazecfg;
        const unsigned long now = millis();
        _renderer.draw(canvas, _maze, _camX, _camY, MazeRenderer::themeFor(_level), _visited);
        const int C = CELL, H = WALL_T / 2;

        // Doors: a barred gate across the way, in their key's colour, a
        // lock in the middle.
        for (int i = 0; i < _activeDoors; i++) {
            const Door &d = _doors[i];
            if (d.open) continue;
            const uint16_t col = keyColour(d.colourId);
            const int bx = sx(d.x * C), by = sy(d.y * C);
            int x, y, w, h;
            if (d.dir == WALL_E || d.dir == WALL_W) {
                x = (d.dir == WALL_E ? bx + C : bx) - H; y = by + H; w = 2 * H; h = C - 2 * H;
            } else {
                x = bx + H; y = (d.dir == WALL_S ? by + C : by) - H; w = C - 2 * H; h = 2 * H;
            }
            if (!onScreen(x, y)) continue;
            canvas.fillRect(x, y, w, h, col);
            if (w > h) for (int k = 2; k < w; k += 3) canvas.drawFastVLine(x + k, y, h, ArcadeConfig::COLOR_BLACK);
            else       for (int k = 2; k < h; k += 3) canvas.drawFastHLine(x, y + k, w, ArcadeConfig::COLOR_BLACK);
            canvas.fillRect(x + w / 2 - 2, y + h / 2 - 2, 4, 4, ArcadeConfig::COLOR_WHITE);
            canvas.drawPixel(x + w / 2 - 1, y + h / 2, ArcadeConfig::COLOR_BLACK);
        }

        // Keys, bobbing.
        for (int i = 0; i < _activeKeys; i++) {
            if (_keys[i].collected) continue;
            const int x = cx(_keys[i].x), y = cy(_keys[i].y) - 1 + ((now / 300 + i) & 1);
            if (onScreen(x, y)) drawKey(canvas, x, y, keyColour(_keys[i].colourId));
        }

        // The exit: a shut hatch, or a pulsing portal once open.
        {
            const int x = sx(_exit.x * C) + H + 1, y = sy(_exit.y * C) + H + 1, w = C - 2 * H - 2;
            if (onScreen(x, y)) {
                if (_exit.unlocked) {
                    static const uint16_t P[3] = { ArcadeConfig::COLOR_GREEN, 0x03E0, ArcadeConfig::COLOR_WHITE };
                    for (int k = 0; k < 3; k++)
                        canvas.drawRect(x + k * 2, y + k * 2, w - k * 4, w - k * 4, P[(k + now / 150) % 3]);
                } else {
                    canvas.fillRect(x, y, w, w, ArcadeConfig::COLOR_GREY);
                    canvas.drawRect(x, y, w, w, ArcadeConfig::COLOR_WHITE);
                    canvas.drawLine(x + 2, y + 2, x + w - 3, y + w - 3, 0x4208);
                    canvas.drawLine(x + w - 3, y + 2, x + 2, y + w - 3, 0x4208);
                }
            }
        }

        // Bombs: a dark ball with a fizzing fuse; red, counting, once lit.
        for (int i = 0; i < _activeBombs; i++) {
            const ProximityBomb &b = _bombs[i];
            if (!b.active) continue;
            const int x = cx(b.x), y = cy(b.y) + 1;
            if (!onScreen(x, y)) continue;
            const uint16_t body = b.fuseActive && now % 200 < 100 ? ArcadeConfig::COLOR_RED : 0x2945;
            canvas.fillCircle(x, y, 5, body);
            canvas.drawPixel(x - 2, y - 2, ArcadeConfig::COLOR_WHITE);
            canvas.drawLine(x + 3, y - 4, x + 5, y - 6, ArcadeConfig::COLOR_AMBER);
            canvas.drawPixel(x + 5 + (now / 90) % 2, y - 7, now % 160 < 80 ? ArcadeConfig::COLOR_YELLOW : ArcadeConfig::COLOR_RED);
            if (b.fuseActive && b.fuseCount > 0) {
                canvas.setTextSize(1);
                canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
                canvas.setCursor(x - 2, y - 3);
                canvas.print(b.fuseCount);
            }
        }

        // Traps: a block with a barrel the way it fires; bullets with a tail.
        for (int i = 0; i < _activeTraps; i++) {
            const TrapEmitter &t = _traps[i];
            uint16_t col = t.type == TrapEmitter::TYPE_A ? ArcadeConfig::COLOR_AMBER : ArcadeConfig::COLOR_MAGENTA;
            if (t.paused() && now % 300 < 150) col = ArcadeConfig::COLOR_GREY;
            const int x = cx(t.x), y = cy(t.y);
            if (onScreen(x, y)) {
                canvas.fillRect(x - 4, y - 4, 9, 9, col);
                canvas.fillRect(x - 2, y - 2, 5, 5, 0x2104);
                canvas.fillRect(x + t.dirX * 4 - 1, y + t.dirY * 4 - 1, 3, 3, col);
                canvas.fillRect(x + t.dirX * 6 - 1, y + t.dirY * 6 - 1, 3, 3, col);
            }
            for (const auto &b : t.bullets) {
                if (!b.active) continue;
                const int bx = sx(b.x), by = sy(b.y);
                if (!onScreen(bx, by)) continue;
                canvas.drawLine(bx - t.dirX * 5, by - t.dirY * 5, bx, by, col);
                canvas.fillRect(bx - 1, by - 1, 3, 3, ArcadeConfig::COLOR_WHITE);
            }
        }

        // Teleport pads: rings turning colour.
        for (int i = 0; i < _activePads; i++) {
            const int x = cx(_pads[i].x), y = cy(_pads[i].y);
            if (!onScreen(x, y)) continue;
            const bool ph = (now / 200 + i) & 1;
            canvas.drawCircle(x, y, 5, ph ? ArcadeConfig::COLOR_CYAN : ArcadeConfig::COLOR_ION_BLUE);
            canvas.drawCircle(x, y, 3, ph ? ArcadeConfig::COLOR_ION_BLUE : ArcadeConfig::COLOR_CYAN);
            canvas.drawPixel(x, y, ArcadeConfig::COLOR_WHITE);
        }

        // Speed boosts: a bolt; time bonuses: a clock.
        for (const auto &b : _boosts) {
            if (!b.active) continue;
            const int x = cx(b.x), y = cy(b.y);
            if (!onScreen(x, y)) continue;
            for (int k = 0; k < 2; k++) {
                canvas.drawLine(x + 2 + k, y - 5, x - 2 + k, y, ArcadeConfig::COLOR_CYAN);
                canvas.drawLine(x - 2 + k, y, x + 2 + k, y, ArcadeConfig::COLOR_CYAN);
                canvas.drawLine(x + 2 + k, y, x - 2 + k, y + 5, ArcadeConfig::COLOR_CYAN);
            }
        }
        for (const auto &b : _bonuses) {
            if (!b.active) continue;
            const int x = cx(b.x), y = cy(b.y);
            if (!onScreen(x, y)) continue;
            canvas.fillCircle(x, y, 5, 0x0320);
            canvas.drawCircle(x, y, 5, ArcadeConfig::COLOR_GREEN);
            canvas.drawLine(x, y, x, y - 3, ArcadeConfig::COLOR_WHITE);
            canvas.drawLine(x, y, x + 2, y + 1, ArcadeConfig::COLOR_WHITE);
        }

        // The player, unless just lost; flashing while safe.
        if (!_dyingUntil && !(safe() && now % 200 < 100))
            mazesprite::draw(canvas, sx(_player.px()), sy(_player.py()), _player.facing, _player.walkFrame, HUD_H);

        _particles.render(canvas, HUD_H, _camX, _camY - HUD_H);

        // The compass: an arrow at the edge, in the colour of what it
        // points to.
        int ax, ay; float dx, dy; uint16_t col;
        if (!_dyingUntil && compassArrow(ax, ay, dx, dy, col)) {
            const int tipX = ax + (int)lroundf(dx * 5), tipY = ay + (int)lroundf(dy * 5);
            const int bx = ax - (int)lroundf(dx * 3), by = ay - (int)lroundf(dy * 3);
            const int pxo = (int)lroundf(-dy * 4), pyo = (int)lroundf(dx * 4);
            canvas.fillTriangle(tipX, tipY, bx + pxo, by + pyo, bx - pxo, by - pyo, col);
            canvas.drawTriangle(tipX, tipY, bx + pxo, by + pyo, bx - pxo, by - pyo, ArcadeConfig::COLOR_BLACK);
        }

        if (_mapOpen) renderMap(canvas);
        if (_demo) drawDemoOverlay(canvas);
        drawHud(canvas);
    }

    // The HUD: level, clock, a square per key (filled once collected),
    // score (an orange T before it on a test run), lives.
    void drawHud(GFXcanvas16 &canvas) {
        using namespace mazecfg;
        const unsigned long now = millis();
        canvas.fillRect(0, 0, ArcadeConfig::PORTRAIT_WIDTH, HUD_H, ArcadeConfig::COLOR_BLACK);
        canvas.setTextSize(1);
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.setCursor(1, 1);   canvas.print("L"); canvas.print(_level);
        canvas.setTextColor(_timeLeft <= 15 && now % 500 < 250 ? ArcadeConfig::COLOR_RED : ArcadeConfig::COLOR_WHITE);
        canvas.setCursor(22, 1);  canvas.print("T"); canvas.print(_timeLeft);
        for (int k = 0; k < _activeKeys; k++) {
            const int x = 50 + k * 6;
            const uint16_t col = keyColour(_keys[k].colourId);
            if (_keys[k].collected) canvas.fillRect(x, 2, 5, 5, col);
            else                    canvas.drawRect(x, 2, 5, 5, col);
        }
        int x = 76;
        if (_test) {
            canvas.setTextColor(ArcadeConfig::COLOR_ORANGE);
            canvas.setCursor(x, 1); canvas.print("T");
            x += 6;
        }
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.setCursor(x, 1);   canvas.print(_score);
        canvas.setCursor(116, 1); canvas.print("x"); canvas.print(_player.lives);
    }

    void drawDemoOverlay(GFXcanvas16 &canvas) {
        canvas.setTextSize(1);
        canvas.fillRect(0, ArcadeConfig::PORTRAIT_HEIGHT - 11, ArcadeConfig::PORTRAIT_WIDTH, 11, ArcadeConfig::COLOR_BLACK);
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.setCursor(3, ArcadeConfig::PORTRAIT_HEIGHT - 9);
        canvas.print("DEMO");
        if (millis() % 1000 < 600) {
            canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
            canvas.setCursor(ArcadeConfig::PORTRAIT_WIDTH - 63, ArcadeConfig::PORTRAIT_HEIGHT - 9);
            canvas.print("A TO START");
        }
    }

    // The stage select: the level's maze, whole, behind its number.
    void renderPicker(GFXcanvas16 &canvas) {
        canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
        renderMap(canvas, true);
        canvas.fillRect(14, 62, 100, 36, ArcadeConfig::COLOR_BLACK);
        canvas.drawRect(14, 62, 100, 36, ArcadeConfig::COLOR_ORANGE);
        char buf[16];
        snprintf(buf, sizeof(buf), "LEVEL %d", _pick);
        hiscore::printCentred(canvas, buf, 68, ArcadeConfig::COLOR_WHITE, 2);
        hiscore::printCentred(canvas, "A: TEST RUN  B: BACK", 88, ArcadeConfig::COLOR_ORANGE);
        hiscore::printCentred(canvas, "STAGE SELECT", 1, ArcadeConfig::COLOR_ORANGE);
    }

    void renderTitle(GFXcanvas16 &canvas) {
        // Blit the 128x160 portrait bitmap into the canvas pixel by pixel —
        // same pattern as Lander Flux's title screen.
        for (int i = 0; i < (ArcadeConfig::PORTRAIT_WIDTH * ArcadeConfig::PORTRAIT_HEIGHT); i++) {
            uint16_t px = pgm_read_word(&maze_flux_128x160_data[i]);
            canvas.drawPixel(i % ArcadeConfig::PORTRAIT_WIDTH,
                             i / ArcadeConfig::PORTRAIT_WIDTH, px);
        }
        // Blinking prompt — visible 600ms out of every 1000ms
        if (millis() % 1000 < 600) {
            const char* prompt = "A TO START";
            canvas.setTextSize(1);
            int16_t tbx, tby; uint16_t tbw, tbh;
            canvas.getTextBounds(prompt, 0, 0, &tbx, &tby, &tbw, &tbh);
            canvas.setTextColor(ArcadeConfig::COLOR_BLACK);
            canvas.setCursor((ArcadeConfig::PORTRAIT_WIDTH - (int16_t)tbw) / 2, 142);
            canvas.print(prompt);
        }
    }

    void renderInstructions(GFXcanvas16 &canvas) {
        canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
        canvas.setTextSize(2);
        canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
        canvas.setTextWrap(false);  // "HOW TO PLAY" is a hair wider than the
                                    // screen at size 2 — clip the last pixel
                                    // or two rather than wrapping to 2 lines
        canvas.setCursor(0, 10);
        canvas.print("HOW TO PLAY");
        canvas.setTextWrap(true);
        canvas.setTextSize(1);
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.setCursor(1, 32); canvas.print("> JOYSTICK TO MOVE");
        canvas.setCursor(1, 43); canvas.print("> KEYS OPEN DOORS");
        canvas.setCursor(1, 54); canvas.print("> LAST KEY: THE EXIT");
        canvas.setCursor(1, 65); canvas.print("> [A] TRAP SWITCH");
        canvas.setCursor(1, 76); canvas.print("> HOLD [B]: MAP");
        canvas.setCursor(1, 87); canvas.print("> AVOID BOMBS+BULLETS");
        canvas.setCursor(1, 98); canvas.print("> BEAT THE CLOCK");

        // High score box — same outline treatment as Lander Flux's info screen
        char scoreStr[32];
        char best[20];
        _scores.bestLine(best, sizeof(best), "");
        snprintf(scoreStr, sizeof(scoreStr), "BEST: %s", best);
        int16_t tbx, tby; uint16_t tbw, tbh;
        canvas.getTextBounds(scoreStr, 0, 0, &tbx, &tby, &tbw, &tbh);

        const int16_t boxX = 4, boxY = 110;
        const int16_t boxW = ArcadeConfig::PORTRAIT_WIDTH - 12, boxH = 28;
        canvas.drawRect(boxX, boxY, boxW, boxH, ArcadeConfig::COLOR_ION_BLUE);

        canvas.setCursor(boxX + (boxW - (int16_t)tbw) / 2, boxY + 9);
        canvas.setTextColor(ArcadeConfig::COLOR_YELLOW);
        canvas.print("BEST: ");
        canvas.setTextColor(ArcadeConfig::COLOR_GREEN);
        canvas.print(best);
    }

    void renderScores(GFXcanvas16 &canvas) {
        canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
        hiscore::drawTable(canvas, _scores.table(), "HIGH SCORES", 30);
        if (millis() % 1000 < 600) hiscore::printCentred(canvas, "A TO START", 130, ArcadeConfig::COLOR_WHITE);
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
        const int rank = _scores.lastRank();
        if (rank >= 0) {
            char buf[24];
            snprintf(buf, sizeof(buf), rank == 0 ? "NEW HIGH SCORE!" : "HIGH SCORE #%d", rank + 1);
            hiscore::printCentred(canvas, buf, 95, ArcadeConfig::COLOR_YELLOW);
        }
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
        if (_endInputArmed) {
            canvas.setCursor(12, 110);
            canvas.print("[BTN A] CONTINUE");
        }
    }

    void flush(GFXcanvas16 &canvas) {
        if (_tft) _tft->drawRGBBitmap(0, 0, canvas.getBuffer(),
            ArcadeConfig::PORTRAIT_WIDTH, ArcadeConfig::PORTRAIT_HEIGHT);
    }

    // A game from level `from`; a test run (the stage select) if test.
    void startGame(AudioEngine &audio, int from = 1, bool test = false) {
        _score = 0; _level = from;
        _test = test; _testFrom = from;
        _player.lives = 3;
        initLevel();
        _state = STATE_PLAYING;
        // Music plays during a game only (through level-complete
        // screens), not on the title screen; it stops at game over.
        audio.loopWAV("/audio/flux-maze.wav");
    }

    // The demo: a random level, three lives, nothing kept.
    void startDemo() {
        _demo = true;
        _test = false;
        _score = 0;
        _level = (int)random(DEMO_MIN_LEVEL, DEMO_MAX_LEVEL + 1);
        _player.lives = 3;
        initLevel();
        _state = STATE_PLAYING;
        _demoUntil = millis() + (unsigned long)random((long)DEMO_MIN_MS, (long)DEMO_MAX_MS + 1);
    }

    // Back to the title, leaving nothing of the demo behind.
    void endDemo() {
        _demo = false;
        _score = 0;
        _level = 1;
        _dyingUntil = 0;
        _state = STATE_TITLE;
        _attractPage = 0;
        _attractTimer = millis();
        _particles.clearAll();
    }

    void enterPicker() {
        _state = STATE_PICK;
        _pick = 1; _pickDir = 0;
        _pickAt = millis();
        _level = _pick;
        initLevel();
    }

    // While a demo runs, new sounds are dropped (lifted again however
    // update() returns).
    struct Silence {
        AudioEngine &a; bool on;
        Silence(AudioEngine &a_, bool on_) : a(a_), on(on_) { if (on) a.setSilenced(true); }
        ~Silence() { if (on) a.setSilenced(false); }
    };

public:
    GameEngineMaze() {}

    void setTFT(Adafruit_ST7735 &tft) { _tft = &tft; }

    void init(AudioEngine &audio) {
        _scores.begin("maze");
        _highScore = (int)_scores.best();
        _score = 0; _level = 1;
        _state        = STATE_TITLE;
        _attractTimer = millis();
        _btnAWasHeld  = true;
        _demo = _test = false;
        struct { const char* path; bool* on; } sounds[] = {
            { PICKUP_WAV, &_pickupOnCard }, { POWERUP_WAV, &_powerupOnCard }, { DOOR_WAV, &_doorOnCard },
            { SWITCH_WAV, &_switchOnCard }, { TELEPORT_WAV, &_teleportOnCard } };
        for (auto &snd : sounds) {
            *snd.on = audio.exists(snd.path);
            if (*snd.on) audio.preload(snd.path);
        }
    }

    // Quitting (the Back button): a game in progress still goes on the
    // table, under the last name entered; a name being entered is kept. A
    // demo or a test run puts nothing there.
    void onQuit(AudioEngine &audio) {
        if (_state == STATE_NAME) _scores.finishNow();
        else if (!_demo && !_test && (_state == STATE_PLAYING || _state == STATE_LEVEL_COMPLETE)) _scores.record(_score);
        audio.mute();
    }

    bool update(GFXcanvas16 &canvas, AudioEngine &audio, const InputState &input) {
        const bool btnA = input.btnA;
        const bool aPressed = btnA && !_prevBtnA;
        _prevBtnA = btnA;

        // ---- DEMO: A plays for real; time up ends it ----
        if (_demo) {
            if (aPressed) { endDemo(); startGame(audio); flush(canvas); return true; }
            if (millis() >= _demoUntil && !_dyingUntil) { endDemo(); renderTitle(canvas); flush(canvas); return true; }
        }

        // ---- NAME ENTRY: then the game-over screen ----
        if (_state == STATE_NAME) {
            canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
            _scores.draw(canvas);
            flush(canvas);
            if (_scores.update(input, 2)) {
                _highScore  = (int)_scores.best();
                _state      = STATE_GAMEOVER;
                _gameOverMs = millis();
                _endInputArmed = false;
            }
            return true;
        }

        // ---- TITLE: title, how to play, high scores, then the demo ----
        if (_state == STATE_TITLE) {
            if (millis() - _attractTimer > ATTRACT_INTERVAL_MS) {
                _attractTimer = millis();
                if (_attractPage == 2) { startDemo(); }
                else _attractPage++;
            }
            if (_state == STATE_TITLE) {
                if (_attractPage == 0)      renderTitle(canvas);
                else if (_attractPage == 1) renderInstructions(canvas);
                else                        renderScores(canvas);
                // A must be let go first, so a press held from the
                // game-over screen doesn't start a run straight away. With
                // B held, A opens the stage select.
                if (_btnAWasHeld) {
                    if (!btnA) _btnAWasHeld = false;
                } else if (btnA && input.btnB) {
                    enterPicker();
                    renderPicker(canvas);
                } else if (btnA) {
                    startGame(audio);
                }
                flush(canvas);
                return true;
            }
        }

        // ---- STAGE SELECT: the stick steps the level (left and right by
        // one, up and down by five), A starts a test run, B or leaving it
        // alone goes back ----
        if (_state == STATE_PICK) {
            if (aPressed) { startGame(audio, _pick, true); flush(canvas); return true; }
            if (input.btnBPressed || millis() - _pickAt > PICK_TIMEOUT_MS) {
                _state = STATE_TITLE; _attractPage = 0; _attractTimer = millis(); _btnAWasHeld = true;
                renderTitle(canvas); flush(canvas);
                return true;
            }
            bool up, down, left, right;
            hiscore::screenDirs(input, 2, up, down, left, right);
            const int dir = right ? 1 : left ? -1 : up ? 5 : down ? -5 : 0;
            bool step = false;
            const unsigned long now = millis();
            if (dir != _pickDir) {
                _pickDir = dir;
                _pickRepeatAt = now + PICK_REPEAT_DELAY_MS;
                step = dir != 0;
            } else if (dir != 0 && (long)(now - _pickRepeatAt) >= 0) {
                _pickRepeatAt = now + PICK_REPEAT_MS;
                step = true;
            }
            if (step) {
                _pick = (_pick - 1 + dir + PICK_LEVELS) % PICK_LEVELS + 1;
                _pickAt = now;
                _level = _pick;
                initLevel();
                audio.playTone(1200, 15);
            }
            renderPicker(canvas);
            flush(canvas);
            return true;
        }

        // ---- GAME OVER and LEVEL COMPLETE: A once it's been let go ----
        if (_state == STATE_GAMEOVER || _state == STATE_LEVEL_COMPLETE) {
            const bool over = _state == STATE_GAMEOVER;
            if (!_endInputArmed && !btnA && millis() - _gameOverMs >= ArcadeConfig::GAMEOVER_INPUT_DELAY_MS)
                _endInputArmed = true;
            if (over) renderGameOver(canvas); else renderLevelComplete(canvas);
            flush(canvas);
            if (over && _test && _endInputArmed && btnA) {
                startGame(audio, _testFrom, true);      // a test run: from its level again
                _prevBtnA = true;
            } else if (over && ((_endInputArmed && btnA) || millis() - _gameOverMs > GAMEOVER_TIMEOUT_MS)) {
                _state         = STATE_TITLE;
                _attractPage   = 0;
                _attractTimer  = millis();
                _btnAWasHeld   = true;
                _test          = false;
            } else if (!over && _endInputArmed && btnA) {
                initLevel();
                _state = STATE_PLAYING;
                _prevBtnA = true;        // that press isn't a switch press
            }
            return true;
        }

        // ---- PLAYING (or the demo) ----
        // Quitting is the cabinet's Back button (main.cpp, then onQuit()).
        Silence silence(audio, _demo);
        bool up, down, left, right, pressA = aPressed;
        if (_demo) {
            uint8_t dir; bool a;
            autopilot(dir, a);
            up = dir == WALL_N; down = dir == WALL_S; left = dir == WALL_W; right = dir == WALL_E;
            pressA = a;
            _mapOpen = false;
        } else {
            hiscore::screenDirs(input, 2, up, down, left, right);
            _mapOpen = input.btnB;
        }
        updatePlaying(up, down, left, right, pressA, audio);
        if (_state == STATE_PLAYING) {
            _particles.update();
            updateCamera(_frameMs);
            markSeen();
            renderPlaying(canvas);
            flush(canvas);
        }
        return true;
    }
};

#endif // GAME_ENGINE_MAZE_H
