#ifndef TANK_FLUX_GAME_H
#define TANK_FLUX_GAME_H

#include "../../games/IGame.h"
#include "../../cabinet/ArcadeConfig.h"
#include <Jet.hpp>

#include "TankFluxConfig.h"
#include "TankMath.h"
#include "TankGeometry.h"
#include "ArenaLayout.h"

// =============================================================================
// TANK FLUX: a Battlezone-style first-person tank game rendered with Jet
// (https://github.com/CubeCoders/Jet). Jet's render config is in
// include/JetConfig.hpp.
//
// Drive around a bounded arena. Obstacles block movement and shells, which
// is what makes them cover. Enemy tanks turn at a capped rate (so they can
// be flanked) and fire shells slow enough to dodge. Repair kits restore
// health. Every BOSS_EVERY_KILLS kills a boss arrives; killing it re-rolls
// the arena layout.
//
// The camera never pitches, so forward is always (sin yaw, 0, cos yaw).
//
// Files: TankFluxGame.cpp (phases, update loop), TankFluxScene.cpp (scene
// and materials), TankFluxArena.cpp (layout -> 3D objects),
// TankFluxPlayer.cpp (driving, shells, kits), TankFluxEnemies.cpp (enemy
// and boss AI), TankFluxHud.cpp (2D overlays and menu screens).
// =============================================================================

namespace tankflux {

class TankFluxGame : public IGame {
public:
    TankFluxGame() {}
    ~TankFluxGame() override { releaseScene(); }

    void init(AudioEngine &audio) override;
    bool update(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) override;
    // The Jet scene (meshes, render queue) is the bulk of this game's heap;
    // it's rebuilt by the next update() after init().
    void onExit() override { releaseScene(); }

    uint8_t getRotation() const override { return 1; }
    const char* getName()  const override { return "Tank Flux"; }

private:
    enum GamePhase { PHASE_ATTRACT, PHASE_PLAYING, PHASE_GAMEOVER };
    enum AttractSlide { SLIDE_GAME, SLIDE_INFO };
    enum EnemyClass : uint8_t { CLASS_1, CLASS_2, CLASS_3 };

    struct RepairKit {
        Renderer::Object* obj = nullptr;
        bool active = true;
        unsigned long respawnAt = 0;
    };

    struct Shell {
        Renderer::Object* obj = nullptr;
        bool  active = false;
        float x = 0, z = 0;
        float vx = 0, vz = 0;    // per frame, set at fire time
        float travelled = 0;
    };

    // Regular tanks and the boss. Everything that differs by type is in
    // `spec`.
    struct Enemy {
        const TankSpec* spec = &REGULAR_TANK;
        Renderer::Object* hull   = nullptr;
        Renderer::Object* turret = nullptr;
        Renderer::Object* barrel = nullptr;
        Renderer::Object* trackL = nullptr;
        Renderer::Object* trackR = nullptr;
        Renderer::Material* barrelMat = nullptr;   // restored after a fire telegraph
        bool  alive = false;
        float x = 0, z = 0;
        float headingDeg = 0;
        unsigned long respawnAt = 0;
        unsigned long nextFireAt = 0;
        EnemyClass tankClass = CLASS_1;
        int hp = 1;
        int maxHp = 1;
        bool playerBumping = false;   // bump damage only on the first frame of contact
        unsigned long fireAt = 0;     // non-zero while telegraphing a shot
        int volley = 0;               // boss: alternates spread/burst
        int burstShotsLeft = 0;       // boss: follow-up shots still to fire
        unsigned long nextBurstAt = 0;
    };

    // --- Session ---------------------------------------------------------------
    GamePhase     _phase = PHASE_ATTRACT;
    AttractSlide  _attractSlide = SLIDE_GAME;
    unsigned long _attractSlideTimer = 0;
    // Attract music loops /audio/tank_loop.wav from SD (silent if missing).
    bool          _attractMusicStarted = false;
    unsigned long _attractMusicEarliestAt = 0;
    bool          _btnBWasHeld = false;     // B must be released before hold-to-exit counts
    unsigned long _btnBHoldStart = 0;       // attract/game over: hold B to exit
    unsigned long _quitHoldStart = 0;       // playing: hold A+B to quit; 0 = not held
    unsigned long _gameOverEnteredMs = 0;
    // See REFERENCE_FRAME_MS: multiplies every per-frame movement.
    float _frameScale = 1.0f;
    unsigned long _lastFrameMs = 0;

    // --- Player ----------------------------------------------------------------
    float _x = 0.0f, _z = 0.0f;
    float _vx = 0.0f, _vz = 0.0f;   // last frame's movement, for enemies that lead their aim
    float _headingDeg = 0.0f;
    float _speed = 0.0f;
    int   _health = HEALTH_MAX;
    int   _score = 0;
    int   _highScore = 0;
    int   _kills = 0;
    int   _level = 1;
    unsigned long _reloadAt = 0;
    unsigned long _muzzleFlashUntil = 0;
    unsigned long _damageFlashUntil = 0;

    // --- Arena -----------------------------------------------------------------
    ArenaLayout _arena;
    RepairKit   _kits[REPAIR_COUNT];
    // The arena-shift chime and flash wait ARENA_SHIFT_CUE_DELAY_MS after a
    // boss kill so they don't cut off the kill fanfare.
    bool          _arenaShiftCuePending = false;
    unsigned long _arenaShiftCueAt = 0;
    unsigned long _arenaShiftFlashUntil = 0;

    // --- Enemies ---------------------------------------------------------------
    Shell _playerShell;
    Shell _enemyShells[MAX_ENEMY_SHELLS];
    Enemy _enemies[MAX_ENEMIES];
    Enemy _boss;
    bool  _bossActive  = false;
    bool  _bossPending = false;            // triggered; alert running or spawn not placed yet
    unsigned long _bossAlertUntil = 0;
    unsigned long _bossKlaxonAt = 0;
    int   _bossKlaxonBeat = 0;
    int   _nextBossAt  = BOSS_EVERY_KILLS;
    int   _bossesDefeated = 0;             // drives the next boss's HP
    unsigned long _bossSpawnedAt = 0;      // for the time bonus
    long  _bossBonus = 0;
    unsigned long _bossBonusUntil = 0;

    // --- Scene -----------------------------------------------------------------
    // Built lazily on the first update(), since Scene needs the canvas buffer.
    Renderer::Scene*  _scene  = nullptr;
    Renderer::Camera  _camera;
    Renderer::Object* _ground = nullptr;
    Renderer::Object* _river  = nullptr;
    Renderer::Object* _obstacleObjs[OBSTACLE_COUNT] = { nullptr };
    Renderer::Object* _treeTrunks[TREE_COUNT]   = { nullptr };
    Renderer::Object* _treeCanopyLo[TREE_COUNT] = { nullptr };
    Renderer::Object* _treeCanopyHi[TREE_COUNT] = { nullptr };
    Renderer::ParticleSystem _particles{ (float)JET32_WORLD_SCALE };
    // Per-row clear colours: sky above the horizon, ground haze below. With
    // pitch locked at 0 the horizon is always at screenHeight/2.
    uint16_t _skyGround[ArcadeConfig::LANDSCAPE_HEIGHT];

    Renderer::DirectionalLight _sun{ Vector3{35, 60, 0}, Renderer::Color{255, 240, 215}, 255 };
    // Ambient is deliberately high (~55%): a shadowed face only gets
    // ambient * colour, and anything darker read as black silhouettes on
    // this LCD. Much higher and the ground checkerboard clips to white.
    Renderer::AmbientLight     _amb{ Renderer::Color{140, 133, 119} };
    // Never registered with the scene: only used for drawSun()'s direction.
    // Lower than _sun because at 60 degrees the disc would always be above
    // the ~38 degree vertical field of view.
    Renderer::DirectionalLight _sunVisual{ Vector3{35, 22, 0}, Renderer::Color{0, 0, 0}, 0 };

    // Colours and shading modes are set in setupMaterials(). Jet's WIREFRAME
    // shading mode isn't implemented, hence a two-tone checkerboard ground.
    Renderer::Material _groundMatA;
    Renderer::Material _groundMatB;
    Renderer::Material _riverMat;
    Renderer::Material _obstacleCubeMat{ 0xFFFF, nullptr, nullptr, false, 255, 255, 30 };
    Renderer::Material _obstaclePyramidMat{ 0xFFFF, nullptr, nullptr, false, 255, 255, 30 };
    // Rocks are irregular cubes: createCapsule is 96 triangles at a usable
    // resolution, against a cube's 12.
    Renderer::Material _obstacleRockMat{ 0xFFFF, nullptr, nullptr, false, 255, 255, 20 };
    Renderer::Material _treeTrunkMat{ 0xFFFF, nullptr, nullptr, false, 255, 255, 10 };
    Renderer::Material _treeCanopyLoMat{ 0xFFFF, nullptr, nullptr, false, 255, 255, 20 };
    Renderer::Material _treeCanopyHiMat{ 0xFFFF, nullptr, nullptr, false, 255, 255, 20 };
    Renderer::Material _kitMat{ ArcadeConfig::COLOR_GREEN };
    // Tanks are UNLIT: lit, the side facing away from the sun went nearly
    // black, and a target you need to spot at range shouldn't depend on
    // which way it faces.
    Renderer::Material _enemyHullMat{ 0xFFFF };
    Renderer::Material _enemyTurretMat{ 0xFFFF };         // class 1
    Renderer::Material _enemyTurretMatClass2{ 0xFFFF };
    Renderer::Material _enemyTurretMatClass3{ 0xFFFF };
    Renderer::Material _enemyBarrelMat{ 0xFFFF };
    Renderer::Material _enemyTrackMat{ 0xFFFF };
    Renderer::Material _bossHullMat{ 0xFFFF };
    Renderer::Material _bossTurretMat{ 0xFFFF };
    Renderer::Material _barrelHotMat{ ArcadeConfig::COLOR_WHITE };   // fire telegraph
    Renderer::Material _playerShellMat{ ArcadeConfig::COLOR_CYAN };
    Renderer::Material _enemyShellMat{ ArcadeConfig::COLOR_AMBER };

    // --- TankFluxGame.cpp ------------------------------------------------------
    void loadHighScore();
    void saveHighScore();
    void recordHighScore();
    void updateFrameScale();
    void startNewGame(AudioEngine &audio);
    bool updateAttract(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    bool updateGameOver(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    bool updatePlaying(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);

    // --- TankFluxScene.cpp -----------------------------------------------------
    void ensureSceneReady(GFXcanvas16 &canvas);
    void releaseScene();
    void buildSkyGround(int h);
    void setupMaterials();
    void buildTankModel(Enemy &e, const TankSpec &spec, Renderer::Material* hullMat,
                        Renderer::Material* turretMat, Renderer::Material* barrelMat);

    // --- TankFluxArena.cpp -----------------------------------------------------
    void generateArenaLayout();
    void repositionObstacle(int i);
    void repositionKit(int i);
    void regenerateArena();

    // --- TankFluxPlayer.cpp ----------------------------------------------------
    void updateDriving(const InputState &input, AudioEngine &audio);
    void resolveEnemyCollision(float &x, float &z, AudioEngine &audio);
    void bumpTank(Enemy &e, float &x, float &z, AudioEngine &audio);
    void tryFire(const InputState &input, AudioEngine &audio);
    void fireShell(Shell &s, float x, float z, float headingDeg, float speed);
    void killShell(Shell &s);
    bool advanceShell(Shell &s, int32_t range);
    void updateShells(AudioEngine &audio);
    void updateKits(AudioEngine &audio);

    // --- TankFluxEnemies.cpp ---------------------------------------------------
    bool isBoss(const Enemy &e) const { return &e == &_boss; }
    bool crowdsTank(const Enemy &self, const Enemy &o, float x, float z, bool spawning) const;
    bool blockedByTank(const Enemy &self, float x, float z, bool spawning) const;
    int enemyCap() const;
    int aliveEnemies() const;
    float enemySpeed() const;
    unsigned long fireDelay() const;
    EnemyClass pickEnemyClass() const;
    void setTankVisible(Enemy &e, bool visible);
    void setBarrelHot(Enemy &e, bool hot);
    void resetFireState(Enemy &e);
    void spawnEnemy(Enemy &e);
    bool trySpawnBoss(AudioEngine &audio);
    void hitEnemy(Enemy &e, AudioEngine &audio, int damage = 1);
    void destroyEnemy(Enemy &e, AudioEngine &audio);
    void playExplosion(AudioEngine &audio);
    void updateEnemies(AudioEngine &audio);
    void updateEnemyAI(Enemy &e, AudioEngine &audio);
    bool fireEnemyShell(Enemy &e, float headingDeg);
    void fireVolley(Enemy &e, AudioEngine &audio);
    void updateTankTransform(Enemy &e);

    // --- TankFluxHud.cpp -------------------------------------------------------
    void drawHUD(GFXcanvas16 &canvas);
    void drawSun(GFXcanvas16 &canvas);
    void drawGunsight(GFXcanvas16 &canvas, int cx, int cy);
    void drawBarrel(GFXcanvas16 &canvas);
    void drawRadar(GFXcanvas16 &canvas);
    void drawFlashes(GFXcanvas16 &canvas);
    void drawBossAlert(GFXcanvas16 &canvas);
    void drawBossBonus(GFXcanvas16 &canvas);
    void drawQuitHint(GFXcanvas16 &canvas);
    void renderAttractGame(GFXcanvas16 &canvas);
    void renderAttractInfo(GFXcanvas16 &canvas);
    void renderGameOver(GFXcanvas16 &canvas);
};

}  // namespace tankflux

using TankFluxGame = tankflux::TankFluxGame;

#endif  // TANK_FLUX_GAME_H
