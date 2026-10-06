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

    // NAME: entering a name for the high-score table, after the last life.
    enum GameState { STATE_TITLE, STATE_PLAYING, STATE_NAME, STATE_GAMEOVER, STATE_LEVEL_COMPLETE };
    GameState _state = STATE_TITLE;

    int  _level     = 1;
    int  _score     = 0;
    int  _highScore = 0;
    int  _levelTime = 0;     // seconds the level starts with
    int  _timeLeft  = 0;

    unsigned long _lastSecondMs = 0;
    unsigned long _lastUpdateMs = 0;
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

    int _camX = 0, _camY = 0;          // the view's top left, maze pixels

    // Which optional sounds are on the card (checked once, in init()).
    bool _pickupOnCard = false, _powerupOnCard = false;
    static constexpr const char* PICKUP_WAV  = "/audio/pickup.wav";
    static constexpr const char* POWERUP_WAV = "/audio/powerup.wav";

    // Out of lives: a name for the table first, if the score made it.
    void endGame(AudioEngine &audio) {
        _scores.forget();
        _state      = _scores.offer(_score) ? STATE_NAME : STATE_GAMEOVER;
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
        int w = 16, h = 20;
        if (_level >= 11) {
            w = constrain(16 + (_level - 11) * 4, 16, MazeGenerator::MAX_W);
            h = constrain(20 + (_level - 11) * 4, 20, MazeGenerator::MAX_H);
        }
        _maze.generate(w, h);

        _player.reset(0, 0);
        _particles.clearAll();
        _activeBombs = _activeTraps = _activeKeys = _activeDoors = _activePads = 0;
        for (auto &b : _boosts)  b.active = false;
        for (auto &b : _bonuses) b.active = false;
        _exit = { _maze.width - 1, _maze.height - 1, false };
        _dyingUntil = 0;
        _safeUntil  = 0;

        measureFromStart();
        placeDoorsAndKeys();
        placeBombs();
        placeTraps();
        placePads();
        placePickups();

        _levelTime    = (int)(TIME_BASE_S + TIME_PER_CELL_S * w * h);
        _timeLeft     = _levelTime;
        _lastSecondMs = _lastUpdateMs = millis();
        updateCamera();
    }

    // Doors across the way from the start to the exit, spaced along it,
    // and a key for each in the stretch before it; the last key, beyond
    // the last door, opens the exit. Keys go off the way where they can,
    // so fetching one is a detour.
    void placeDoorsAndKeys() {
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
        static bool onPath[MAX_CELLS];
        memset(onPath, 0, sizeof(onPath));
        for (int i = 0; i < len; i++) onPath[path[i]] = true;

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

        // A key per stretch: off the way if possible.
        for (int k = 0; k <= _activeDoors; k++) {
            int kx = 0, ky = 0;
            bool found = false;
            for (int attempt = 0; attempt < 300 && !found; attempt++) {
                if (!randomFree(kx, ky, k, START_CLEAR)) break;
                found = !onPath[cellIndex(kx, ky)] || attempt > 150;
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
    // Camera: the player centred, within the maze.
    // -------------------------------------------------------------------------
    void updateCamera() {
        using namespace mazecfg;
        const int mw = _maze.width * CELL + 1, mh = _maze.height * CELL + 1;
        _camX = mw <= VIEW_W ? (mw - VIEW_W) / 2 : constrain((int)_player.px() - VIEW_W / 2, 0, mw - VIEW_W);
        _camY = mh <= VIEW_H ? (mh - VIEW_H) / 2 : constrain((int)_player.py() - VIEW_H / 2, 0, mh - VIEW_H);
    }

    // -------------------------------------------------------------------------
    // Play
    // -------------------------------------------------------------------------
    bool safe() const { return millis() < _safeUntil; }

    void sfxPickup(AudioEngine &audio) {
        if (_pickupOnCard) audio.playWAV(PICKUP_WAV);
        else audio.playPowerUpExtraLife();
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
        if (--_player.lives <= 0) { endGame(audio); return; }
        _player.reset(0, 0);
        _safeUntil = millis() + mazecfg::RESPAWN_SAFE_MS;
        for (int i = 0; i < _activeTraps; i++) _traps[i].clearBullets();
        for (int i = 0; i < _activeBombs; i++) _bombs[i].disarm();
        if (_deathCause == DEATH_TIME) _timeLeft = _levelTime;
        _lastSecondMs = millis();
    }

    void completeLevel(AudioEngine &audio) {
        using namespace mazecfg;
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
            sfxPickup(audio);
            for (int d = 0; d < _activeDoors; d++) if (_doors[d].colourId == k.colourId) _doors[d].open = true;
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
            audio.playTone(440, 150);
            break;
        }
        if (_exit.unlocked && x == _exit.x && y == _exit.y) completeLevel(audio);
    }

    void updatePlaying(bool up, bool down, bool left, bool right, bool aPressed, AudioEngine &audio) {
        using namespace mazecfg;
        const unsigned long now = millis();
        const unsigned long dt = min(now - _lastUpdateMs, 100UL);
        _lastUpdateMs = now;

        if (_dyingUntil) {
            if (now >= _dyingUntil) finishDying(audio);
            return;
        }

        if (now - _lastSecondMs >= 1000UL) {
            _lastSecondMs += 1000UL;
            if (--_timeLeft <= 0) { _timeLeft = 0; die(DEATH_TIME, audio); return; }
        }

        _player.update(up, down, left, right,
                       [this](int x, int y, uint8_t dir) { return canPass(x, y, dir); });
        if (_player.arrived) arriveAt(_player.x, _player.y, audio);
        if (_state != STATE_PLAYING) return;

        // A in or beside a type B trap's line of fire: its switch.
        if (aPressed) {
            for (int i = 0; i < _activeTraps; i++) {
                TrapEmitter &t = _traps[i];
                if (t.type != TrapEmitter::TYPE_B) continue;
                if (t.switchReach(_player.nearX(), _player.nearY())) {
                    t.activateSwitch();
                    audio.playTone(523, 80);
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

    void renderPlaying(GFXcanvas16 &canvas) {
        using namespace mazecfg;
        _renderer.draw(canvas, _maze, _camX, _camY);
        const int C = CELL;

        // Doors: a bar across the way, in their key's colour.
        for (int i = 0; i < _activeDoors; i++) {
            const Door &d = _doors[i];
            if (d.open) continue;
            const int x = sx(d.x * C), y = sy(d.y * C);
            const uint16_t col = keyColour(d.colourId);
            if (d.dir == WALL_E)      canvas.fillRect(x + C - 1, y + 1, 3, C - 1, col);
            else if (d.dir == WALL_W) canvas.fillRect(x - 1, y + 1, 3, C - 1, col);
            else if (d.dir == WALL_S) canvas.fillRect(x + 1, y + C - 1, C - 1, 3, col);
            else                      canvas.fillRect(x + 1, y - 1, C - 1, 3, col);
        }

        for (int i = 0; i < _activeKeys; i++) {
            if (_keys[i].collected) continue;
            canvas.fillRect(sx(_keys[i].x * C) + 2, sy(_keys[i].y * C) + 2, 4, 4, keyColour(_keys[i].colourId));
        }

        {
            const uint16_t col = _exit.unlocked ? (millis() % 500 < 250 ? ArcadeConfig::COLOR_GREEN : ArcadeConfig::COLOR_BLACK)
                                                : ArcadeConfig::COLOR_WHITE;
            canvas.drawRect(sx(_exit.x * C) + 1, sy(_exit.y * C) + 1, 6, 6, col);
        }

        for (int i = 0; i < _activeBombs; i++) {
            const ProximityBomb &b = _bombs[i];
            if (!b.active) continue;
            const uint16_t col = b.fuseActive ? (millis() % 200 < 100 ? ArcadeConfig::COLOR_RED : ArcadeConfig::COLOR_YELLOW)
                                              : ArcadeConfig::COLOR_AMBER;
            canvas.fillCircle(sx(b.x * C) + 4, sy(b.y * C) + 4, 3, col);
        }

        for (int i = 0; i < _activeTraps; i++) {
            const TrapEmitter &t = _traps[i];
            uint16_t col = t.type == TrapEmitter::TYPE_A ? ArcadeConfig::COLOR_AMBER : ArcadeConfig::COLOR_MAGENTA;
            if (t.paused() && millis() % 300 < 150) col = ArcadeConfig::COLOR_GREY;
            canvas.fillRect(sx(t.x * C) + 2, sy(t.y * C) + 2, 4, 4, col);
            for (const auto &b : t.bullets)
                if (b.active) canvas.fillRect(sx(b.x) - 1, sy(b.y) - 1, 2, 2, ArcadeConfig::COLOR_WHITE);
        }

        for (int i = 0; i < _activePads; i++) {
            const uint16_t col = millis() % 600 < 300 ? ArcadeConfig::COLOR_CYAN : ArcadeConfig::COLOR_ION_BLUE;
            canvas.drawCircle(sx(_pads[i].x * C) + 4, sy(_pads[i].y * C) + 4, 3, col);
        }

        for (const auto &b : _boosts)
            if (b.active) canvas.drawTriangle(sx(b.x * C) + 4, sy(b.y * C) + 1, sx(b.x * C) + 1, sy(b.y * C) + 7,
                                              sx(b.x * C) + 7, sy(b.y * C) + 7, ArcadeConfig::COLOR_CYAN);
        for (const auto &b : _bonuses)
            if (b.active) canvas.drawRect(sx(b.x * C) + 2, sy(b.y * C) + 1, 4, 6, ArcadeConfig::COLOR_GREEN);

        // The player, unless just lost; flashing while safe.
        if (!_dyingUntil && !(safe() && millis() % 200 < 100)) {
            const int x = sx(_player.px()) - 4, y = sy(_player.py()) - 4;
            canvas.fillRect(x + 2, y + 2, 4, 4, ArcadeConfig::COLOR_WHITE);
            switch (_player.facing) {
                case PlayerMaze::FACE_UP:    canvas.drawFastHLine(x + 3, y + 1, 2, ArcadeConfig::COLOR_CYAN); break;
                case PlayerMaze::FACE_DOWN:  canvas.drawFastHLine(x + 3, y + 6, 2, ArcadeConfig::COLOR_CYAN); break;
                case PlayerMaze::FACE_LEFT:  canvas.drawFastVLine(x + 1, y + 3, 2, ArcadeConfig::COLOR_CYAN); break;
                case PlayerMaze::FACE_RIGHT: canvas.drawFastVLine(x + 6, y + 3, 2, ArcadeConfig::COLOR_CYAN); break;
            }
        }

        _particles.render(canvas, HUD_H, _camX, _camY - HUD_H);

        // HUD
        canvas.fillRect(0, 0, ArcadeConfig::PORTRAIT_WIDTH, HUD_H, ArcadeConfig::COLOR_BLACK);
        canvas.setTextSize(1);
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.setCursor(1, 1);   canvas.print("L:"); canvas.print(_level);
        canvas.setTextColor(_timeLeft <= 15 && millis() % 500 < 250 ? ArcadeConfig::COLOR_RED : ArcadeConfig::COLOR_WHITE);
        canvas.setCursor(32, 1);  canvas.print("T:"); canvas.print(_timeLeft);
        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.setCursor(68, 1);  canvas.print(_score);
        canvas.setCursor(110, 1); canvas.print("x"); canvas.print(_player.lives);
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
        canvas.setCursor(1, 34); canvas.print("> JOYSTICK TO MOVE");
        canvas.setCursor(1, 46); canvas.print("> KEYS OPEN DOORS");
        canvas.setCursor(1, 58); canvas.print("> LAST KEY: THE EXIT");
        canvas.setCursor(1, 70); canvas.print("> [A] TRAP SWITCH");
        canvas.setCursor(1, 82); canvas.print("> AVOID BOMBS+BULLETS");
        canvas.setCursor(1, 94); canvas.print("> BEAT THE CLOCK");

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

    void startGame(AudioEngine &audio) {
        _score = 0; _level = 1;
        _player.lives = 3;
        initLevel();
        _state = STATE_PLAYING;
        // Music plays during a game only (through level-complete
        // screens), not on the title screen; it stops at game over.
        audio.loopWAV("/audio/flux-maze.wav");
    }

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
        _pickupOnCard  = audio.exists(PICKUP_WAV);
        _powerupOnCard = audio.exists(POWERUP_WAV);
        if (_pickupOnCard)  audio.preload(PICKUP_WAV);
        if (_powerupOnCard) audio.preload(POWERUP_WAV);
    }

    // Quitting (the Back button): a game in progress still goes on the
    // table, under the last name entered; a name being entered is kept.
    void onQuit(AudioEngine &audio) {
        if (_state == STATE_NAME) _scores.finishNow();
        else if (_state == STATE_PLAYING || _state == STATE_LEVEL_COMPLETE) _scores.record(_score);
        audio.mute();
    }

    bool update(GFXcanvas16 &canvas, AudioEngine &audio, const InputState &input) {
        const bool btnA = input.btnA;
        const bool aPressed = btnA && !_prevBtnA;
        _prevBtnA = btnA;

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

        // ---- TITLE: title, how to play, high scores ----
        if (_state == STATE_TITLE) {
            if (millis() - _attractTimer > ATTRACT_INTERVAL_MS) {
                _attractPage  = (_attractPage + 1) % 3;
                _attractTimer = millis();
            }
            if (_attractPage == 0)      renderTitle(canvas);
            else if (_attractPage == 1) renderInstructions(canvas);
            else                        renderScores(canvas);

            // A must be let go first, so a press held from the game-over
            // screen doesn't start a run straight away.
            if (_btnAWasHeld) {
                if (!btnA) _btnAWasHeld = false;
            } else if (btnA) {
                startGame(audio);
            }
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
            if (over && ((_endInputArmed && btnA) || millis() - _gameOverMs > GAMEOVER_TIMEOUT_MS)) {
                _state         = STATE_TITLE;
                _attractTimer  = millis();
                _btnAWasHeld   = true;
            } else if (!over && _endInputArmed && btnA) {
                initLevel();
                _state = STATE_PLAYING;
                _prevBtnA = true;        // that press isn't a switch press
            }
            return true;
        }

        // ---- PLAYING ----
        // Quitting is the cabinet's Back button (main.cpp, then onQuit()).
        bool up, down, left, right;
        hiscore::screenDirs(input, 2, up, down, left, right);
        updatePlaying(up, down, left, right, aPressed, audio);
        if (_state == STATE_PLAYING) {
            _particles.update();
            updateCamera();
            renderPlaying(canvas);
            flush(canvas);
        }
        return true;
    }
};

#endif // GAME_ENGINE_MAZE_H
