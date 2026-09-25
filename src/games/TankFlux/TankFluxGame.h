#ifndef TANK_FLUX_GAME_H
#define TANK_FLUX_GAME_H

#include "../../games/IGame.h"
#include "../../cabinet/ArcadeConfig.h"
#include <Preferences.h>
#include <math.h>

#include <Jet.hpp>

#include "TankFluxConfig.h"
#include "TankGeometry.h"

// =============================================================================
// TANK FLUX — a Battlezone-style first-person tank game rendered with Jet
// (https://github.com/CubeCoders/Jet).
//
// You drive a tank around a bounded arena of wireframe ground and solid
// obstacles. Obstacles block movement (and, once shells exist, block shots —
// that's what makes them cover rather than scenery). Repair kits scattered
// around the arena restore health, so there's a reason to cross open ground
// rather than sit in one place.
//
// The camera sits inside the tank and never pitches, so the aim-forward
// vector collapses to the pitch=0 case of Jet's actual rotation
// convention, checked directly against Camera::getRotationMatrix() and
// Camera::lookAt() rather than assumed: (sin(yaw), 0, cos(yaw)).
//
// Enemy tanks turn at a capped rate, which is what makes them flankable, and
// fire shells slow enough to drive out of the way of. Shells die on contact
// with obstacles, so cover works without needing any separate line-of-sight
// test. Damage is a 100-point pool; repair kits restore it.
//
// Jet's per-frontend render config lives in include/JetConfig.hpp at the
// project root.
// =============================================================================

namespace tankflux {

class TankFluxGame : public IGame {
private:

    // --- Obstacles -----------------------------------------------------------
    // Sizes/shapes are still hand-tuned per slot (kept below), but positions
    // are no longer fixed: generateArenaLayout() (see there) randomizes x/z
    // for every obstacle, tree and repair kit at scene setup and again on
    // every boss kill, rejecting any spot too close to the river, the
    // player, a live tank, or another placed object. These initializer
    // values only matter as a fallback if that search can't find room
    // (see placeCircle()) — not constexpr, since generateArenaLayout()
    // overwrites x/z in place.
    enum ObstacleShape : uint8_t { SHAPE_CUBE, SHAPE_PYRAMID, SHAPE_ROCK };
    struct ObstacleDef { int32_t x, z, size; ObstacleShape shape; };
    ObstacleDef OBSTACLES[OBSTACLE_COUNT] = {
        { -1500,   800, 440, SHAPE_CUBE    },
        {   900,  1500, 380, SHAPE_PYRAMID },
        {  1900,  -400, 500, SHAPE_ROCK    },
        {  -600, -1600, 360, SHAPE_PYRAMID },
        { -2200, -1100, 420, SHAPE_ROCK    },
        {  2100,  1900, 340, SHAPE_PYRAMID },
        {   200,  2300, 460, SHAPE_CUBE    },
        { -1900,  2000, 360, SHAPE_ROCK    },
        {  1300, -1900, 420, SHAPE_CUBE    },
        {  -300,  -600, 300, SHAPE_PYRAMID },
        {  2600, -1400, 260, SHAPE_ROCK    },
        { -2500,  1500, 300, SHAPE_ROCK    },
    };

    struct RepairDef { int32_t x, z; };
    RepairDef REPAIRS[REPAIR_COUNT] = {
        {  2400,   600 },
        { -2400,  -300 },
        {   500, -2400 },
    };

    // Decorative pines, inspired by the tiered low-poly trees in Jet's own
    // Woodland example (github.com/CubeCoders/JetExamples/esp32-lod-billboards)
    // — a trunk plus two stacked canopy cones, built from proven Jet
    // primitives (see buildPineTree()). Positions randomized the same way as
    // OBSTACLES/REPAIRS (see generateArenaLayout()) — they don't block
    // movement or shots, but still shouldn't visually clip through the river
    // or crowd the other placed scenery. Six is a deliberately modest count:
    // 24 triangles per tree adds up fast against the render queue's heap
    // headroom that caused the earlier bad_alloc crash.
    struct TreeDef { int32_t x, z; };
    TreeDef TREES[TREE_COUNT] = {
        { -1000, -2000 },
        {  2700,  2700 },
        { -2700,  2700 },
        {  2700, -2700 },
        {     0,  1250 },
        {  2600,     0 },
    };

    enum GamePhase { PHASE_ATTRACT, PHASE_PLAYING, PHASE_GAMEOVER };
    GamePhase _phase = PHASE_ATTRACT;

    // Attract mode rotates between two slides, same convention as
    // AsteroidFluxGame (SLIDE_SPLASH/SLIDE_INFO, 8s each). SLIDE_GAME is a
    // placeholder — there's no real gameplay screenshot yet — swap
    // renderAttractGame() for one once there's actual art to show.
    enum AttractSlide { SLIDE_GAME, SLIDE_INFO };
    AttractSlide  _attractSlide      = SLIDE_GAME;
    unsigned long _attractSlideTimer = 0;
    // Same convention as AsteroidFluxGame: loops a WAV from SD rather than
    // synthesizing tones. An earlier version re-triggered the cabinet's
    // generated-tone boot riff on a timer instead — mechanically similar
    // (both loop), but playtest feedback was that the repeating chiptune
    // blip read as annoying rather than as music, and that it should sound
    // like the other games' actual audio tracks, not square-wave tones.
    // Requires sd_assets/tank_flux/tank_loop.wav on the SD card (8kHz mono
    // 8-bit unsigned PCM, per the README's Audio section) — silently does
    // nothing if the file isn't there yet, the same as any other game's SD
    // asset would.
    bool _attractMusicStarted = false;
    // Grace period before the attract loop is allowed to start — see the
    // guard at its call site for why this is needed in addition to the
    // isSamplePlaying()/isMelodyPlaying() checks.
    unsigned long _attractMusicEarliestAt = 0;

    struct RepairKit {
        Renderer::Object* obj = nullptr;
        bool active = true;
        unsigned long respawnAt = 0;
    };
    RepairKit _kits[REPAIR_COUNT];

    struct Shell {
        Renderer::Object* obj = nullptr;
        bool  active = false;
        float x = 0, z = 0;
        float vx = 0, vz = 0;    // per-frame delta, baked at fire time
        float travelled = 0;
    };
    Shell _playerShell;
    Shell _enemyShells[MAX_ENEMY_SHELLS];

    enum EnemyClass : uint8_t { CLASS_1, CLASS_2, CLASS_3 };

    struct Enemy {
        Renderer::Object* hull   = nullptr;
        Renderer::Object* turret = nullptr;
        Renderer::Object* barrel = nullptr;
        Renderer::Object* trackL = nullptr;
        Renderer::Object* trackR = nullptr;
        bool  alive = false;
        float x = 0, z = 0;
        float headingDeg = 0;
        unsigned long respawnAt = 0;
        unsigned long nextFireAt = 0;
        EnemyClass tankClass = CLASS_1;
        int hp = 1;
        int maxHp = 1;
        bool playerBumping = false;   // rising-edge guard for resolveEnemyCollision()'s bump damage
        unsigned long fireAt = 0;     // non-zero while telegraphing a shot
        int volley = 0;               // boss only: alternates spread/burst
        int burstShotsLeft = 0;       // boss only: follow-up shots in a burst
        unsigned long nextBurstAt = 0;
    };
    Enemy _enemies[MAX_ENEMIES];

    // The boss reuses the Enemy struct (same AI/movement shape) but is a
    // one-off outside the regular pool — see BOSS_EVERY_KILLS.
    Enemy _boss;
    bool  _bossActive  = false;
    bool  _bossPending = false;   // trigger fired but perimeter spawn hasn't found a spot yet
    unsigned long _bossAlertUntil = 0;   // updateEnemies() won't call trySpawnBoss() before this
    unsigned long _bossKlaxonAt = 0;     // next alert beep
    int _bossKlaxonBeat = 0;
    int   _nextBossAt  = BOSS_EVERY_KILLS;
    int   _bossesDefeated = 0;   // drives the next boss's HP — see BOSS_HP_STEP
    unsigned long _bossSpawnedAt = 0;   // start of the current fight, for the time bonus
    long  _bossBonus = 0;               // last time bonus awarded, for drawBossBonus()
    unsigned long _bossBonusUntil = 0;

    // Arena-shift transition cue (regenerateArena()) — deferred the same way
    // _bossAlertUntil is, so the new-arena chime doesn't cut off the boss's
    // own kill fanfare (destroyEnemy(), a 300ms tone) before it's heard.
    bool _arenaShiftCuePending = false;
    unsigned long _arenaShiftCueAt = 0;
    unsigned long _arenaShiftFlashUntil = 0;

    unsigned long _quitHoldStart = 0;   // 0 = A+B not currently both held

    // --- Jet scene state -----------------------------------------------------
    // Scene needs a framebuffer pointer, which only exists once update() hands
    // us the launcher's canvas, so everything is built lazily on the first
    // update() call rather than in init().
    Renderer::Scene*  _scene  = nullptr;
    Renderer::Camera  _camera;
    Renderer::Object* _ground = nullptr;
    Renderer::Object* _river  = nullptr;
    Renderer::Object* _obstacleObjs[OBSTACLE_COUNT] = { nullptr };
    // Vector3 is declared at global scope in Jet (Shader.hpp), unlike Color.
    Renderer::DirectionalLight _sun{ Vector3{35, 60, 0}, Renderer::Color{255, 240, 215}, 255 };
    // Ambient history, since this has been tuned three times now on
    // realism-first assumptions that kept being wrong for what's actually a
    // tiny, often-photographed-through-a-phone-camera LCD:
    //   - 235/(60,66,90): flat and bright — hills didn't read at all.
    //   - 255/(38,42,58): correctly let directional shading show, but the
    //     blue-heavy tint read as "purple/grey" on any face angled away
    //     from the sun (jetModulateRGB565 adds ambient to brightness BEFORE
    //     the colour multiply, so it's the ONLY lighting a shadowed face
    //     gets) — and a many-sided obstacle or tree canopy shows several
    //     such faces at any camera angle.
    //   - 255/(46,44,40): fixed the colour cast but was still far too dark
    //     — hardware photos after that change still showed obstacles as
    //     near-solid black silhouettes with the checkerboard barely
    //     visible. A shadowed face only gets ambient*colour/255, so a
    //     genuinely photorealistic ambient level (15-20%) just isn't
    //     legible on this hardware/format, however correct the shading math
    //     is. This game wants "always readable," not "physically lit."
    //   - 255/(100,95,85): playtest confirmed this direction was right —
    //     trees finally read as green — and asked for still more.
    // Now ~55% (140,133,119, same neutral ratio): checked directly against
    // buildTerrain()'s actual exaggerated brightness range (16-252 before
    // ambient) rather than just nudged further blind — at this level ~75%
    // of ground cells clip to full white already, so this is close to the
    // ceiling before the checkerboard shading stops reading as shading at
    // all and just becomes a flat bright slab again (the original
    // complaint, from the opposite direction). Obstacle/tree faces have
    // more headroom since their normals face every direction, not mostly
    // up, so they clip less than the ground does.
    Renderer::AmbientLight     _amb{ Renderer::Color{140, 133, 119} };
    // A second, unregistered DirectionalLight purely to get its computed
    // worldLightDir for drawSun() — never passed to setDirectionalLight, so
    // it has no effect on shading. Deliberately a lower elevation (22° vs
    // _sun's 60°) than the light that actually shades the world: this
    // camera never pitches and its vertical FOV is only ~38° each way
    // (setFOV(88, width) on a 160x128 canvas), so a disc placed at the
    // real 60° elevation projects off the top of the screen on every
    // heading — not a rare framing issue, geometrically impossible to see.
    // Tropical Island's own LensFlare example takes the same approach for
    // the same reason: "Decoupled from the shadow/shading light so each
    // can be tuned independently."
    Renderer::DirectionalLight _sunVisual{ Vector3{35, 22, 0}, Renderer::Color{0, 0, 0}, 0 };
    // Ground is a two-tone checkerboard, not a wireframe grid: Jet declares
    // ShadingMode::WIREFRAME in its enum but never implements it anywhere in
    // the rasterizer, so a "wireframe" material silently renders as a solid
    // fill. (Rasterizer::wireframeMode is real, but it's a global debug
    // toggle that forces a black background, which would throw away the
    // sky/ground split below.) Colours are assigned in ensureSceneReady.
    Renderer::Material _groundMatA;
    Renderer::Material _groundMatB;
    // Colour assigned in ensureSceneReady.
    Renderer::Material _riverMat;
    // Obstacles are lit (GOURAUD): their faces shade differently as you
    // drive around them, a real depth cue in a first-person game where the
    // camera is constantly moving. One material per shape rather than one
    // shared material, purely for colour variety — all still warm-toned,
    // since the ground is green-grey and the sky is blue, so warm is the
    // one hue on screen that can't be mistaken for either. Colours assigned
    // in ensureSceneReady.
    Renderer::Material _obstacleCubeMat{ 0xFFFF, nullptr, nullptr, false, 255, 255, 30 };
    Renderer::Material _obstaclePyramidMat{ 0xFFFF, nullptr, nullptr, false, 255, 255, 30 };
    // "Rocks" are irregular cubes, not round primitives: createCapsule costs
    // 96 triangles at segments=6 (checked directly, not assumed) against a
    // cube's 12, and segments=3 — the enforced minimum — produces an
    // open-ended tube with no end caps at all (latSegments = 90/angleStep
    // truncates to 0). Not affordable at any usable resolution on this
    // hardware, so irregular proportions + rotation do the visual work
    // instead, at the same triangle cost as any other obstacle.
    Renderer::Material _obstacleRockMat{ 0xFFFF, nullptr, nullptr, false, 255, 255, 20 };
    // Back to bright green per explicit request, now that the ground/
    // obstacles are lit brightly enough that white was no longer needed to
    // stand out. UNLIT either way, so it's the same bright green regardless
    // of viewing angle.
    Renderer::Material _kitMat{ ArcadeConfig::COLOR_GREEN };
    // Two bright UNLIT tones rather than one lit material: lighting left the
    // side facing away from the sun almost black, and a target you have to
    // spot at range shouldn't depend on which way it happens to be facing.
    // Different hull/turret tones keep the silhouette readable without it.
    Renderer::Material _enemyHullMat{ 0xFFFF };
    Renderer::Material _enemyTurretMat{ 0xFFFF };
    // Bright neutral grey, not dark gunmetal: playtest feedback was that
    // the barrel made no visible difference, and a dark UNLIT colour on
    // this hardware/format is exactly the mistake this session already
    // made (and fixed) for the ground and obstacles — it just disappears
    // against the also-dark background instead of reading as a separate
    // part. Kept neutral/cool rather than warm so it stays visually
    // distinct from the hull's red and turret's amber.
    Renderer::Material _enemyBarrelMat{ 0xFFFF };
    // Tracks: a thin strip flanking each side of the hull, so it isn't
    // just a plain box — playtest feedback verbatim.
    Renderer::Material _enemyTrackMat{ 0xFFFF };
    // Class 2/3 turret colours: swapped onto an enemy's existing turret
    // mesh at spawn (see setObjectMaterial()) rather than built as separate
    // geometry — Jet has no live per-object scale, so class visually reads
    // through colour, not size. _enemyTurretMat itself stays class 1's
    // olive drab. Steel blue-grey for class 2, warning yellow for class 3
    // — distinct from each other and from class 1, at similar brightness
    // to the materials already tuned this session (no dark tones).
    Renderer::Material _enemyTurretMatClass2{ 0xFFFF };
    Renderer::Material _enemyTurretMatClass3{ 0xFFFF };
    // Boss: its own materials on its own (larger) geometry — deep vivid
    // red hull to read as more dangerous than the regular red, neutral
    // grey turret/armour for contrast.
    Renderer::Material _bossHullMat{ 0xFFFF };
    Renderer::Material _bossTurretMat{ 0xFFFF };
    Renderer::Material _playerShellMat{ ArcadeConfig::COLOR_CYAN };
    Renderer::Material _enemyShellMat{ ArcadeConfig::COLOR_AMBER };
    // Swapped onto an enemy's barrel while it telegraphs a shot.
    Renderer::Material _barrelHotMat{ ArcadeConfig::COLOR_WHITE };
    // Pine trees: GOURAUD like the other obstacles, so they pick up the
    // same live _sun/_amb lighting instead of a baked colour — the two
    // canopy tiers get slightly different greens so the step between them
    // reads as two tiers rather than one lumpy cone, the same trick
    // Woodland's tree generator uses per profile segment.
    Renderer::Material _treeTrunkMat{ 0xFFFF, nullptr, nullptr, false, 255, 255, 10 };
    Renderer::Material _treeCanopyLoMat{ 0xFFFF, nullptr, nullptr, false, 255, 255, 20 };
    Renderer::Material _treeCanopyHiMat{ 0xFFFF, nullptr, nullptr, false, 255, 255, 20 };
    Renderer::Object* _treeTrunks[TREE_COUNT]    = { nullptr };
    Renderer::Object* _treeCanopyLo[TREE_COUNT]  = { nullptr };
    Renderer::Object* _treeCanopyHi[TREE_COUNT]  = { nullptr };
    Renderer::ParticleSystem _particles{ (float)JET32_WORLD_SCALE };

    // Per-row background colours: sky above the horizon, ground below, which
    // Scene uses for the frame clear. With pitch locked at 0 the true horizon
    // sits at exactly screenHeight/2 every frame (a ground point at infinite
    // distance projects there), so a fixed split is always correct — and it
    // means the world still reads as ground below the horizon even past the
    // far edge of the ground mesh.
    uint16_t _skyGround[ArcadeConfig::LANDSCAPE_HEIGHT];

    // --- Tank state ----------------------------------------------------------
    float _x = 0.0f, _z = 0.0f;
    float _vx = 0.0f, _vz = 0.0f;   // last frame's actual movement, for leading enemy aim
    float _headingDeg = 0.0f;
    float _speed = 0.0f;

    int _health = HEALTH_MAX;
    int _score = 0;
    int _highScore = 0;
    int _kills = 0;
    int _level = 1;

    unsigned long _reloadAt = 0;
    unsigned long _muzzleFlashUntil = 0;
    unsigned long _damageFlashUntil = 0;

    bool _btnBWasHeld = false;
    unsigned long _gameOverEnteredMs = 0;

    Preferences _prefs;


    // A placed obstacle/tree/kit's own footprint, tracked only for the
    // duration of generateArenaLayout() so later picks can avoid earlier ones.
    struct PlacedCircle { float x, z, r; };

    // TankFluxGame.cpp
    void loadHighScore();
    void saveHighScore();
    void startNewGame(AudioEngine &audio);

    // TankFluxScene.cpp
    void buildSkyGround(int h);
    void ensureSceneReady(GFXcanvas16 &canvas);

    // TankFluxArena.cpp
    static int32_t obstacleRadius(const ObstacleDef &o);
    bool placeCircle(float radius, const PlacedCircle* placed, int placedCount,
                     float &outX, float &outZ);
    void generateArenaLayout();
    void repositionObstacle(int i);
    void repositionKit(int i);
    void regenerateArena();
    bool blockedFor(float x, float z, int32_t radius) const;
    void resolveObstacleCollision(float &x, float &z) const;

    // TankFluxPlayer.cpp
    void updateDriving(const InputState &input, AudioEngine &audio);
    void resolveEnemyCollision(float &x, float &z, AudioEngine &audio);
    void bumpTank(Enemy &e, int32_t enemyRadius, float &x, float &z, AudioEngine &audio);
    void fireShell(Shell &s, float x, float z, float headingDeg, float speed);
    void killShell(Shell &s);
    bool advanceShell(Shell &s, int32_t range);
    void updateShells(AudioEngine &audio);
    void tryFire(const InputState &input, AudioEngine &audio);
    void updateKits(AudioEngine &audio);

    // TankFluxEnemies.cpp
    int32_t tankRadius(const Enemy &e) const;
    bool crowdsTank(const Enemy &self, const Enemy &o, float x, float z, bool spawning) const;
    bool blockedByTank(const Enemy &self, float x, float z, bool spawning) const;
    int enemyCap() const;
    int aliveEnemies() const;
    float enemySpeed() const;
    unsigned long fireDelay() const;
    EnemyClass pickEnemyClass() const;
    void setBarrelHot(Enemy &e, bool hot);
    void resetFireState(Enemy &e);
    void spawnEnemy(Enemy &e);
    void hitEnemy(Enemy &e, AudioEngine &audio, int damage = 1);
    void destroyEnemy(Enemy &e, AudioEngine &audio);
    bool trySpawnBoss(AudioEngine &audio);
    void updateEnemyAI(Enemy &e, float speed, AudioEngine &audio);
    bool fireEnemyShell(Enemy &e, float headingDeg);
    void fireVolley(Enemy &e, AudioEngine &audio);
    void updateEnemyTransform(Enemy &e);
    void updateBossTransform();
    void updateEnemies(AudioEngine &audio);

    // TankFluxHud.cpp
    void drawHUD(GFXcanvas16 &canvas);
    void drawSun(GFXcanvas16 &canvas);
    void drawGunsight(GFXcanvas16 &canvas, int cx, int cy);
    void drawBarrel(GFXcanvas16 &canvas);
    void drawRadar(GFXcanvas16 &canvas);
    void drawDamageFlash(GFXcanvas16 &canvas);
    void drawArenaShiftFlash(GFXcanvas16 &canvas);
    void drawBossAlert(GFXcanvas16 &canvas);
    void drawBossBonus(GFXcanvas16 &canvas);
    void drawQuitHint(GFXcanvas16 &canvas);
    void renderAttractGame(GFXcanvas16 &canvas);
    void renderAttractInfo(GFXcanvas16 &canvas);

public:
    TankFluxGame() {}

    void init(AudioEngine &audio) override;

    bool update(GFXcanvas16 &canvas,
                const InputState &input,
                AudioEngine &audio) override;

    uint8_t getRotation() const override { return 1; }
    const char* getName()  const override { return "Tank Flux"; }
};

}  // namespace tankflux

using TankFluxGame = tankflux::TankFluxGame;

#endif // TANK_FLUX_GAME_H
