#ifndef TANK_FLUX_GAME_H
#define TANK_FLUX_GAME_H

#include "../../games/IGame.h"
#include "../../cabinet/ArcadeConfig.h"
#include "../../assets/shared/SharedAssets.h"
#include "assets/TitleScreen.h"
#include <Preferences.h>
#include <math.h>

#include <Jet.hpp>

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

class TankFluxGame : public IGame {
private:
    // --- Arena ---------------------------------------------------------------
    static const int32_t ARENA_HALF   = 3000;   // playable area is +/- this in X and Z
    // The ground is ONE static mesh covering the whole bounded arena, built
    // once at world origin and never repositioned. An earlier version
    // re-centred it on the tank in discrete steps — a trick carried over
    // from a design where the play space scrolled endlessly, which isn't
    // true here: the arena is bounded, obstacles and repair kits already
    // sit at fixed world coordinates, so a mesh sized to cover the whole
    // arena plus the fog band needs no repositioning at all. That also
    // fixed a real correctness problem the recentring approach had: hills
    // (hillHeight()) and the river both need to agree on what "the same
    // point in the world" means, which a mesh that moves under them could
    // not guarantee.
    //
    // Sizing: from the worst case (a tank in one corner looking straight out
    // through the opposite side) the mesh needs a half-extent of at least
    // ARENA_HALF + depthFogFar plus margin = 3000 + 3600 + margin. Cell
    // COUNT, not physical size, drives triangle count — the ground still
    // dominates it, and every queued triangle costs ~100 bytes in Jet's
    // render queue, a single contiguous allocation that has already run this
    // hardware out of contiguous heap space once. So the size below is
    // generous (real headroom past the minimum) while CELLS stays low.
    static const int32_t GROUND_SIZE  = 14400;   // half-extent 7200
    static const int32_t GROUND_CELLS = 12;      // 242 triangles

    // River: a fixed landmark strip, not tied to the tank's position the
    // way the terrain is. RIVER_Y sits a few units above the terrain's own
    // surface (which itself varies by hillHeight()) so the two meshes never
    // go exactly coplanar and flicker against each other.
    static const int32_t RIVER_Y       = 6;
    static const int32_t RIVER_WIDTH   = 260;
    static const int32_t RIVER_SEGMENTS = 8;

    // --- Tank ----------------------------------------------------------------
    static const int32_t EYE_HEIGHT  = 120;
    static const int32_t TANK_RADIUS = 150;

    // Both signs are here as named constants because this project has had to
    // flip joystick axes by trial more than once — the cabinet's stick is
    // physically mounted rotated relative to a landscape (rotation 1) game,
    // so X/Y read swapped, same as AsteroidFluxGame. If driving or turning
    // comes out backwards on hardware, flip the relevant sign here.
    static constexpr float DRIVE_SIGN = -1.0f;   // flipped after playtest
    static constexpr float TURN_SIGN  = 1.0f;

    static constexpr float TURN_RATE    = 2.2f;   // deg/frame at full deflection
    static constexpr float FWD_SPEED    = 26.0f;  // world units/frame
    static constexpr float REV_SPEED    = 14.0f;  // reverse is deliberately slower
    static constexpr float SPEED_SMOOTH = 0.2f;   // 0=no response, 1=instant
    // Hold BTN B: joyX strafes sideways instead of driving forward/back.
    // Same sign-flip caveat as DRIVE_SIGN above — flip here if strafe comes
    // out backwards on hardware. Turning (joyY) is untouched, so you can
    // strafe and adjust aim at the same time.
    static constexpr float STRAFE_SIGN  = 1.0f;
    static constexpr float STRAFE_SPEED = 20.0f;  // world units/frame

    // --- Health / repair kits ------------------------------------------------
    static const int HEALTH_MAX = 100;
    static const int REPAIR_AMOUNT = 30;
    static const int REPAIR_COUNT  = 3;
    static const int32_t REPAIR_PICKUP_RADIUS = 240;
    static const unsigned long REPAIR_RESPAWN_MS = 12000;

    static const unsigned long GAMEOVER_TIMEOUT_MS = 30000UL;

    // --- Combat --------------------------------------------------------------
    // Shells fly flat at a fixed height: gameplay is entirely on the ground
    // plane, so there's no reason to carry a Y velocity around.
    static const int32_t SHELL_Y = 95;

    static constexpr float PLAYER_SHELL_SPEED = 70.0f;   // units/frame
    static const int32_t   PLAYER_SHELL_RANGE = 2600;
    // One shell in flight at a time, Battlezone-style — it's what stops the
    // fire button being a mash and makes each shot a decision.
    static const unsigned long PLAYER_RELOAD_MS = 400;

    static const int MAX_ENEMIES = 3;
    static const int32_t ENEMY_RADIUS   = 170;
    // ~half the hull's 280 width, referenced from a low-poly tank model
    // the user found (barrel length / hull width ratio there was ~0.62).
    static const int32_t BARREL_LENGTH  = 170;
    // Track strips flanking the 280-wide hull: thin, low, and a little
    // longer than the hull's own 380 depth for a slight overhang. Offset
    // hugs the hull's outer edge with a small overlap so there's no gap.
    static const int32_t TRACK_WIDTH    = 36;
    static const int32_t TRACK_HEIGHT   = 50;
    static const int32_t TRACK_DEPTH    = 400;
    static const int32_t TRACK_OFFSET   = 130;
    static constexpr float ENEMY_SPEED      = 11.0f;
    // The turn rate cap is the whole reason enemies are beatable: it's what
    // lets you flank one that's already committed to a heading.
    static constexpr float ENEMY_TURN_RATE  = 1.3f;
    static constexpr float ENEMY_AIM_TOLERANCE = 12.0f;  // degrees before it shoots
    static const int32_t ENEMY_FIRE_RANGE = 2200;
    // Kept well out rather than closing to point blank: a shell fired from
    // close range can't be driven out of the way of, so an enemy parked on
    // top of you would be unavoidable damage rather than a threat to play
    // around.
    static const int32_t ENEMY_STANDOFF   = 900;
    // Cadence at level 1; escalation tightens it (see fireDelay). Slack on
    // purpose — the pool is only five hits deep and shells from off-screen
    // are the hardest thing in the game to answer.
    static const unsigned long ENEMY_FIRE_MIN_MS = 3600;
    static const unsigned long ENEMY_FIRE_MAX_MS = 6200;
    static const unsigned long ENEMY_FIRE_FLOOR_MS = 1900;
    static const unsigned long ENEMY_RESPAWN_MS  = 3500;

    // --- Escalation ----------------------------------------------------------
    // The run opens with a single tank and earns its way up to three. This is
    // both the difficulty curve and the pacing fix: three simultaneous
    // attackers from the first second left no room to learn the arena.
    static const int KILLS_PER_LEVEL = 4;
    static const int MAX_LEVEL = 6;

    static const int MAX_ENEMY_SHELLS = 4;
    // Slow enough that breaking sideways actually outruns the shell: you need
    // to clear HIT_RADIUS before it arrives, so this is the number that
    // decides whether "keep moving broadside" is a real defence or a
    // suggestion.
    static constexpr float ENEMY_SHELL_SPEED = 32.0f;
    static const int32_t   ENEMY_SHELL_RANGE = 2600;

    static const int HIT_DAMAGE = 20;
    static const int32_t HIT_RADIUS  = 190;   // enemy shell vs player
    static const int32_t KILL_RADIUS = 250;   // player shell vs enemy
    static const int SCORE_PER_KILL = 100;

    // --- Enemy classes & boss --------------------------------------------
    // Class 1 is the original single-hit tank, unchanged. Classes 2/3 only
    // enter the mix once _level puts more than one tank on the field at
    // once (see pickEnemyClass()), so a tougher tank shows up as a
    // variation within an already-multi-enemy fight rather than a surprise
    // sprung on a lone opening attacker. No enemy healing/repair-kit use —
    // the player has no visibility into enemy HP (no bar, by design, to
    // keep the HUD simple), so a tank quietly undoing damage would read as
    // unfair rather than clever.
    static constexpr int CLASS_HP[3] = { 1, 2, 3 };

    // Boss: a one-off, separate from the regular MAX_ENEMIES pool, so the
    // field doesn't backfill with regular tanks mid-fight. Every 15 kills
    // (kept simple/round; regular-kill count only — see destroyEnemy(),
    // the boss's own death doesn't count toward its own next trigger).
    // Dimensions/offsets are the regular tank's own values x1.6 throughout.
    static const int BOSS_EVERY_KILLS = 15;
    static const int BOSS_HP = 8;
    static const int BOSS_SCORE = 1000;
    static const int32_t BOSS_RADIUS      = 270;
    static const int32_t BOSS_KILL_RADIUS = 400;
    // Backstop minimum for trySpawnBoss()'s opposite-side placement — see
    // there for why "always far" is a targeted angle, not just a distance
    // floor on a random one.
    static const int32_t BOSS_MIN_SPAWN_DIST = 2200;
    static const int32_t BOSS_SPAWN_JITTER_DEG = 40;
    static const int32_t BOSS_HULL_W = 448, BOSS_HULL_H = 176, BOSS_HULL_D = 608;
    static const int32_t BOSS_TURRET_W = 240, BOSS_TURRET_H = 144;
    static const int32_t BOSS_BARREL_R = 32, BOSS_BARREL_LEN = 272;
    static const int32_t BOSS_TRACK_W = 58, BOSS_TRACK_H = 80, BOSS_TRACK_D = 640, BOSS_TRACK_OFFSET = 208;
    static const int32_t BOSS_HULL_Y = 88, BOSS_TURRET_Y = 248, BOSS_TRACK_Y = 48;

    // Covers the whole arena corner-to-corner, so nothing is ever off-dial.
    static const int32_t RADAR_RANGE = 4300;

    // --- Obstacles -----------------------------------------------------------
    // Hand-placed rather than random: a fixed arena is learnable, so players
    // can come to know where cover is instead of re-reading the map each run.
    enum ObstacleShape : uint8_t { SHAPE_CUBE, SHAPE_PYRAMID, SHAPE_ROCK };
    struct ObstacleDef { int32_t x, z, size; ObstacleShape shape; };
    static const int OBSTACLE_COUNT = 12;
    static constexpr ObstacleDef OBSTACLES[OBSTACLE_COUNT] = {
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
    static constexpr RepairDef REPAIRS[REPAIR_COUNT] = {
        {  2400,   600 },
        { -2400,  -300 },
        {   500, -2400 },
    };

    // Decorative pines, inspired by the tiered low-poly trees in Jet's own
    // Woodland example (github.com/CubeCoders/JetExamples/esp32-lod-billboards)
    // — a trunk plus two stacked canopy cones, built from proven Jet
    // primitives (see buildPineTree()). Hand-placed like OBSTACLES/REPAIRS,
    // clear of both by 500+ units and of the river, so they read as
    // scenery rather than a hidden collision hazard — they don't block
    // movement or shots. Six is a deliberately modest count: 24 triangles
    // per tree adds up fast against the render queue's heap headroom that
    // caused the earlier bad_alloc crash.
    struct TreeDef { int32_t x, z; };
    static const int TREE_COUNT = 6;
    static constexpr TreeDef TREES[TREE_COUNT] = {
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
    };
    Enemy _enemies[MAX_ENEMIES];

    // The boss reuses the Enemy struct (same AI/movement shape) but is a
    // one-off outside the regular pool — see BOSS_EVERY_KILLS.
    Enemy _boss;
    bool  _bossActive  = false;
    bool  _bossPending = false;   // trigger fired but perimeter spawn hasn't found a spot yet
    int   _nextBossAt  = BOSS_EVERY_KILLS;

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

    void loadHighScore() {
        _prefs.begin("tf_data", true);
        _highScore = _prefs.getInt("highscore", 0);
        _prefs.end();
    }

    void saveHighScore() {
        _prefs.begin("tf_data", false);
        _prefs.putInt("highscore", _highScore);
        _prefs.end();
    }

    static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
        return (uint16_t)((r << 11) | (g << 5) | b);
    }

    void buildSkyGround(int h) {
        const int horizon = h / 2;
        for (int y = 0; y < h; ++y) {
            if (y < horizon) {
                // Deep blue overhead fading to pale haze at the horizon.
                float t = (float)y / (float)horizon;
                _skyGround[y] = rgb565((uint8_t)(2  + t * 13.0f),
                                       (uint8_t)(8  + t * 34.0f),
                                       (uint8_t)(18 + t * 13.0f));
            } else {
                // Ground hazes out toward the horizon and darkens close in,
                // so the checkerboard mesh has something to sit against.
                // Raised well off near-black: with depth fog now finishing
                // well short of this background showing through at all,
                // this colour is still what most of the visible ground
                // band fades toward at typical viewing distance (a low,
                // near-horizontal view covers a lot of far terrain even a
                // little below the horizon), so it needs to read as lit
                // terrain haze on its own, not a dark void the checkerboard
                // pops out of.
                float t = (float)(y - horizon) / (float)(h - horizon);
                _skyGround[y] = rgb565((uint8_t)(16 - t * 5.0f),
                                       (uint8_t)(34 - t * 10.0f),
                                       (uint8_t)(19 - t * 6.0f));
            }
        }
    }

    static int32_t obstacleRadius(const ObstacleDef &o) {
        // Effective circular footprint for a square-ish block; a little under
        // its half-diagonal so you can just scrape past a corner.
        return (o.size * 3) / 5;
    }

    // Cosmetic height field for the terrain — two overlaid sine waves at
    // different frequencies/phases, chosen only to avoid an obviously
    // periodic single-wave look.
    //
    // Deliberately NOT sampled anywhere else: the tank's camera stays on a
    // fixed EYE_HEIGHT plane (updateDriving()) and collision is a flat 2D
    // distance test (blockedFor()), so a hill tall enough to matter can
    // still visibly poke through the tank's fixed driving height near it.
    // A real fix would mean sampling terrain height under the tank and
    // every enemy each frame — real added cost for what's currently a
    // Tier-1 cosmetic pass. Worth revisiting if hills are pushed taller
    // still.
    static int32_t hillHeight(int32_t wx, int32_t wz) {
        float fx = (float)wx, fz = (float)wz;
        float h = 70.0f * sinf(fx * 0.0009f) * cosf(fz * 0.0011f)
                + 38.0f * sinf(fx * 0.0021f + 1.7f) * sinf(fz * 0.0017f);
        return (int32_t)h;
    }

    // Same spacing and per-cell material alternation as
    // Primitives::createGrid, but with real per-face geometry: playtest
    // feedback was that the hills were barely visible even though the
    // height field was there. The root cause wasn't amplitude, it was
    // shading — the first version kept createGrid's convention of sharing
    // each vertex between neighbouring cells with a hardcoded straight-up
    // normal, which is correct for a genuinely flat plane but means a
    // slope has NO shading cue at all: UNLIT ignores normals entirely, and
    // even lit shading with a wrong-but-uniform normal can't show a bump.
    //
    // Fixed properly rather than just raising the amplitude further: each
    // cell now gets its own 4 unique vertices (no sharing across cells)
    // with a normal computed from the actual cross product of that cell's
    // two edges, and the material is FLAT rather than UNLIT so the
    // renderer actually uses it. FLAT rather than GOURAUD deliberately —
    // with per-face (not shared) vertices, every vertex of a triangle
    // already carries the same normal, so GOURAUD's per-vertex lighting
    // would compute an identical result at up to 3x the cost.
    //
    // This does duplicate vertices relative to a shared-vertex grid (~4x),
    // but that cost lands in Object::vertices (built once, not per frame)
    // rather than Jet's render queue, which only scales with TRIANGLE
    // count — unchanged at 242 — and is the thing that actually ran this
    // hardware out of contiguous heap once.
    //
    // Built once at world origin and never repositioned (see the ARENA
    // comment on GROUND_SIZE for why), so these coordinates ARE true world
    // coordinates — buildRiverStrip() calls the same hillHeight() at the
    // same world positions, so the river's surface always agrees with the
    // terrain under it rather than needing to track a moving mesh.
    Renderer::Object* buildTerrain(int32_t width, int32_t height, int32_t rows, int32_t cols,
                                   Renderer::Material* matA, Renderer::Material* matB) {
        Renderer::Object* grid = new Renderer::Object();
        int32_t hw = width / 2, hh = height / 2;
        int32_t rowSpacing = height / rows, colSpacing = width / cols;

        for (int32_t r = 0; r < rows - 1; ++r) {
            for (int32_t c = 0; c < cols - 1; ++c) {
                int32_t x0 = c * colSpacing - hw,       z0 = r * rowSpacing - hh;
                int32_t x1 = (c + 1) * colSpacing - hw, z1 = (r + 1) * rowSpacing - hh;
                int32_t y00 = hillHeight(x0, z0), y10 = hillHeight(x1, z0);
                int32_t y11 = hillHeight(x1, z1), y01 = hillHeight(x0, z1);

                // Two edges of the cell: along +x (columns) and along +z
                // (rows). cross(edgeZ, edgeX) points up for a near-flat
                // surface (verified by construction: with dy terms at 0
                // its Y component reduces to +rowSpacing*colSpacing).
                //
                // SHADE_EXAGGERATION inflates the height deltas used ONLY
                // for this normal calculation — the actual vertex Y below
                // still uses the real y00/y10/y11/y01, so geometry/gameplay
                // are untouched. Verified numerically (not guessed): at
                // 1200-unit cell spacing, hillHeight()'s real slope tilts
                // the true normal by under 8 degrees at its steepest, which
                // after jetShadeBrightness's squared falloff produced a
                // brightness range of only ~161-210 (out of 285) across the
                // whole grid — checkerboard cells were all in the same
                // narrow midtone band, reading as uniformly flat no matter
                // how the material colours or ambient were tuned. An 8x
                // exaggeration here (same technique as normal-map bump
                // exaggeration) widens that to ~16-252, a real lit/shadow
                // split, while the hills themselves stay exactly as subtle
                // as before.
                const float SHADE_EXAGGERATION = 8.0f;
                float e1y = (float)(y10 - y00) * SHADE_EXAGGERATION;
                float e2y = (float)(y01 - y00) * SHADE_EXAGGERATION;
                float nx = -(float)rowSpacing * e1y;
                float ny =  (float)rowSpacing * (float)colSpacing;
                float nz = -(float)colSpacing * e2y;
                float len = sqrtf(nx * nx + ny * ny + nz * nz);
                float s = (len > 0.0001f) ? ((float)FIXED_POINT_SCALE / len) : 0.0f;
                Vector3 normal{ (int32_t)(nx * s), (int32_t)(ny * s), (int32_t)(nz * s) };

                uint16_t b = (uint16_t)grid->vertices.size();
                grid->addVertex(Renderer::Object::Vertex{ Vector3{x0, y00, z0}, Vector2{0, 0}, normal });
                grid->addVertex(Renderer::Object::Vertex{ Vector3{x1, y10, z0}, Vector2{0, 0}, normal });
                grid->addVertex(Renderer::Object::Vertex{ Vector3{x1, y11, z1}, Vector2{0, 0}, normal });
                grid->addVertex(Renderer::Object::Vertex{ Vector3{x0, y01, z1}, Vector2{0, 0}, normal });

                Renderer::Material* mat = (r + c) % 2 == 0 ? matA : matB;
                grid->addFace(b, b + 1, b + 2, b + 3, mat);
            }
        }
        grid->calculateBoundingBox();
        return grid;
    }

    // A cheap low-poly pine, inspired by the tiered trees in Jet's own
    // Woodland example (github.com/CubeCoders/JetExamples/esp32-lod-billboards)
    // — a trunk with a two-tier canopy above it — but built entirely from
    // Jet's own proven primitives (createCube/createPyramid, the same calls
    // the obstacles already use) rather than hand-rolled geometry, so there's
    // no new winding/normal code to get wrong. 12 + 6 + 6 = 24 triangles per
    // tree, against Woodland's own 60-220: this hardware doesn't have the
    // queue headroom for their full LOD system, so there's just the one
    // fixed representation. `trunkH`/`loH`/`hiH` are local Y extents;
    // objects created here still need `setPosition()` by the caller.
    void buildPineTree(int32_t x, int32_t groundY, int32_t z,
                       Renderer::Material* trunkMat,
                       Renderer::Material* loMat, Renderer::Material* hiMat,
                       Renderer::Object*& outTrunk,
                       Renderer::Object*& outLo, Renderer::Object*& outHi) {
        const int32_t trunkW = 34, trunkH = 90;
        const int32_t loBase = 260, loH = 190;
        const int32_t hiBase = 150, hiH = 150;

        outTrunk = Primitives::createCube(trunkW, trunkH, trunkW, trunkMat);
        outTrunk->setPosition(x, groundY + trunkH / 2, z);

        // Base overlaps a little into the trunk top so there's no gap if
        // the trunk sways or the canopy doesn't sit perfectly flush.
        outLo = Primitives::createPyramid(loBase, loH, loMat);
        outLo->setPosition(x, groundY + trunkH - 10, z);

        // Base sits partway up the lower cone rather than at its apex, so
        // the two tiers read as a visible step rather than one smooth cone
        // — the same per-tier flare Woodland's profile-sweep produces.
        outHi = Primitives::createPyramid(hiBase, hiH, hiMat);
        outHi->setPosition(x, groundY + trunkH - 10 + loH / 2, z);
    }

    // A short, fixed-position decorative strip — not tied to the tank's
    // position the way the terrain is, since it's meant to be a real arena
    // landmark you navigate around rather than a texture that scrolls with
    // you. Purely visual for now: no collision, no gameplay effect. Sits a
    // few units above the terrain's own surface to avoid the two coplanar
    // meshes flickering against each other (z-fighting) where they overlap.
    Renderer::Object* buildRiverStrip(int32_t x0, int32_t z0, int32_t x1, int32_t z1,
                                      int32_t width, int32_t segments,
                                      Renderer::Material* mat) {
        Renderer::Object* strip = new Renderer::Object();
        float dx = (float)(x1 - x0), dz = (float)(z1 - z0);
        float len = sqrtf(dx * dx + dz * dz);
        float ux = dx / len, uz = dz / len;          // unit vector along the river
        float px = -uz, pz = ux;                     // perpendicular (across the river)
        float hw = (float)width / 2.0f;

        for (int32_t i = 0; i <= segments; ++i) {
            float t = (float)i / (float)segments;
            float cx = (float)x0 + dx * t, cz = (float)z0 + dz * t;
            addRiverVertex(strip, cx + px * hw, cz + pz * hw, (float)i / (float)segments);
            addRiverVertex(strip, cx - px * hw, cz - pz * hw, (float)i / (float)segments);
        }
        for (int32_t i = 0; i < segments; ++i) {
            int32_t v0 = i * 2, v1 = v0 + 1, v2 = v0 + 3, v3 = v0 + 2;
            strip->addFace(v0, v1, v2, v3, mat);
        }
        strip->calculateBoundingBox();
        return strip;
    }

    // Y follows hillHeight() at this point plus RIVER_Y, rather than a flat
    // constant — the terrain undulates by up to ~60 units, and a river at a
    // fixed absolute height would sink visibly below it wherever a hill
    // rises.
    static void addRiverVertex(Renderer::Object* strip, float x, float z, float v) {
        int32_t ix = (int32_t)x, iz = (int32_t)z;
        strip->addVertex(Renderer::Object::Vertex{
            Vector3{ix, hillHeight(ix, iz) + RIVER_Y, iz},
            Vector2{0, (uint16_t)(v * FIXED_POINT_SCALE)},
            Vector3{0, FIXED_POINT_SCALE, 0}
        });
    }

    void ensureSceneReady(GFXcanvas16 &canvas) {
        if (_scene) return;

        _scene = new Renderer::Scene(canvas.getBuffer(), nullptr,
                                     canvas.width(), canvas.height());
        _scene->setClearBuffer(true);
        buildSkyGround(canvas.height());
        _scene->backgroundGradientColors = _skyGround;

        // Wide on purpose. At 70 a tank slid out of frame almost as soon as
        // you started turning toward it, which made a head-on attacker
        // impossible to track while manoeuvring. The extra peripheral vision
        // costs some perspective distortion at the edges and is worth it.
        _camera.setFOV(88, canvas.width());
        _camera.nearPlane = 48;
        _camera.farPlane  = 5200;
        _scene->setCamera(&_camera);

        // Two ground tones that differ enough to actually read as a
        // checkerboard at this resolution — an earlier attempt in Combat Flux
        // used two near-identical navies and the floor looked like one flat
        // slab. Kept greener than the obstacles' warm tones so obstacles pop.
        //
        // Brightened after playtest feedback that hill shading was
        // invisible even with correct per-face normals and FLAT shading
        // wired up (verified against Renderer.cpp's actual jetShadeBrightness
        // — the lighting math itself was right). The real ceiling was these
        // colours: at R=5-9/31, G=16-26/63, shading has almost no headroom
        // to show against a base that dark regardless of how correct the
        // normals are. Still deliberately darker/cooler than the obstacles'
        // warm tones so obstacles keep popping against it.
        _groundMatA.color = rgb565(11, 30, 15);
        _groundMatB.color = rgb565(17, 44, 21);
        // R:G ratios tuned so these stay tan/amber under full lighting
        // instead of reading red: the previous values (23,33,13) and
        // especially (26,24,10) had G too small a fraction of its own
        // 6-bit range relative to R's fraction of its 5-bit range, so a
        // brightly-lit face — worked out numerically against
        // jetModulateRGB565, not guessed — converged toward a reddish-pink
        // rather than staying warm tan/amber (this is what playtest
        // reported as pyramids having "a red bottom": their most directly
        // lit face).
        _obstacleCubeMat.color    = rgb565(25, 41, 14);   // warm tan
        _obstaclePyramidMat.color = rgb565(28, 38, 6);    // dry amber
        // Brightened after playtest feedback that rocks blended into the
        // terrain — the original (15,15,12) was close enough in luminance
        // to the dark ground (max channel ~26) to read as barely distinct.
        // Real contrast, not just a different hue.
        _obstacleRockMat.color    = rgb565(25, 26, 21);   // light stone grey
        _riverMat.color        = rgb565(9, 24, 27);       // pale blue-green
        _enemyHullMat.color    = rgb565(31,  6,  4);   // vivid red
        // Was amber (31,22,4) — playtest feedback was that hull+turret
        // both just read as "orange" together, since amber is still in
        // the same red-family hue as the hull, just lighter. Olive-drab
        // is a real tank colour and, more importantly here, a genuinely
        // different hue (green-dominant, not red-dominant) rather than
        // another shade of the same one.
        _enemyTurretMat.color  = rgb565(16, 28, 8);    // olive drab
        _enemyBarrelMat.color  = rgb565(22, 44, 22);   // bright neutral silver-grey
        // Balanced so R:G:B match their channel depths at the same
        // brightness fraction (32% of each channel's own max) — a
        // genuinely neutral dark grey rather than the slightly purple
        // cast an uneven ratio gives at low brightness (the same issue
        // diagnosed for the ground/ambient earlier this session).
        _enemyTrackMat.color   = rgb565(10, 20, 10);   // neutral dark grey
        _enemyTurretMatClass2.color = rgb565(14, 26, 30);  // steel blue-grey
        _enemyTurretMatClass3.color = rgb565(30, 40, 4);   // warning yellow
        _bossHullMat.color     = rgb565(24, 3, 3);     // deep vivid red
        _bossTurretMat.color   = rgb565(18, 17, 16);   // neutral armour grey
        _treeTrunkMat.color    = rgb565(15, 21, 7);    // bark brown
        _treeCanopyLoMat.color = rgb565(5, 25, 7);     // deep pine green
        _treeCanopyHiMat.color = rgb565(11, 37, 10);   // brighter sunlit tip

        // FLAT, not UNLIT: the terrain has real per-face slope normals now
        // (see buildTerrain()), and FLAT is what actually uses them to
        // shade hills instead of rendering them as a flat, uniform colour
        // regardless of height.
        _groundMatA.shadingMode         = Renderer::ShadingMode::FLAT;
        _groundMatB.shadingMode         = Renderer::ShadingMode::FLAT;
        _obstacleCubeMat.shadingMode    = Renderer::ShadingMode::GOURAUD;
        _obstaclePyramidMat.shadingMode = Renderer::ShadingMode::GOURAUD;
        _obstacleRockMat.shadingMode    = Renderer::ShadingMode::GOURAUD;
        _treeTrunkMat.shadingMode       = Renderer::ShadingMode::GOURAUD;
        _treeCanopyLoMat.shadingMode    = Renderer::ShadingMode::GOURAUD;
        _treeCanopyHiMat.shadingMode    = Renderer::ShadingMode::GOURAUD;
        _riverMat.shadingMode           = Renderer::ShadingMode::UNLIT;
        _enemyHullMat.shadingMode   = Renderer::ShadingMode::UNLIT;
        _enemyTurretMat.shadingMode = Renderer::ShadingMode::UNLIT;
        _enemyBarrelMat.shadingMode = Renderer::ShadingMode::UNLIT;
        _enemyTrackMat.shadingMode  = Renderer::ShadingMode::UNLIT;
        _enemyTurretMatClass2.shadingMode = Renderer::ShadingMode::UNLIT;
        _enemyTurretMatClass3.shadingMode = Renderer::ShadingMode::UNLIT;
        _bossHullMat.shadingMode    = Renderer::ShadingMode::UNLIT;
        _bossTurretMat.shadingMode  = Renderer::ShadingMode::UNLIT;
        _kitMat.shadingMode         = Renderer::ShadingMode::UNLIT;
        _playerShellMat.shadingMode = Renderer::ShadingMode::UNLIT;
        _enemyShellMat.shadingMode  = Renderer::ShadingMode::UNLIT;

        _scene->setDirectionalLight(&_sun);
        _scene->setAmbientLight(&_amb);

        _ground = buildTerrain(GROUND_SIZE, GROUND_SIZE, GROUND_CELLS, GROUND_CELLS,
                               &_groundMatA, &_groundMatB);
        _scene->addObject(_ground);

        _river = buildRiverStrip(-2900, -2700, 1900, 2900, RIVER_WIDTH, RIVER_SEGMENTS, &_riverMat);
        _river->cullingMode = Renderer::CullingMode::NO_CULLING;   // hand-authored winding, unverified
        _scene->addObject(_river);

        for (int i = 0; i < OBSTACLE_COUNT; ++i) {
            const ObstacleDef &o = OBSTACLES[i];
            // Ground is no longer flat (hillHeight()), so every obstacle's
            // resting height is the local terrain height plus however far
            // its own shape needs lifting to sit on top of it rather than
            // through it. One-time cost at scene build, not per frame.
            int32_t groundY = hillHeight(o.x, o.z);
            Renderer::Object* obj;
            Renderer::Material* mat;
            int32_t baseY;
            switch (o.shape) {
                case SHAPE_PYRAMID:
                    // createPyramid puts its base at local y=0.
                    mat = &_obstaclePyramidMat;
                    obj = Primitives::createPyramid(o.size, (o.size * 5) / 4, mat);
                    baseY = groundY;
                    break;
                case SHAPE_ROCK:
                    // "Rock" = an irregular cube (non-uniform proportions +
                    // rotation), not a round primitive — see the material
                    // declaration for why createCapsule isn't affordable
                    // here. createCube centres on its own origin, so it
                    // needs lifting by half its height.
                    mat = &_obstacleRockMat;
                    obj = Primitives::createCube((o.size * 9) / 10, (o.size * 11) / 20,
                                                 (o.size * 6) / 5, mat);
                    obj->setRotation(0, (o.x * 7 + o.z * 3) % 360, 0);  // deterministic "random" facing
                    baseY = groundY + (o.size * 11) / 40;
                    break;
                default:  // SHAPE_CUBE
                    mat = &_obstacleCubeMat;
                    obj = Primitives::createCube(o.size, (o.size * 3) / 4, o.size, mat);
                    baseY = groundY + (o.size * 3) / 8;
                    break;
            }
            obj->setPosition(o.x, baseY, o.z);
            _scene->addObject(obj);
            _obstacleObjs[i] = obj;
        }

        for (int i = 0; i < TREE_COUNT; ++i) {
            const TreeDef &t = TREES[i];
            buildPineTree(t.x, hillHeight(t.x, t.z), t.z,
                         &_treeTrunkMat, &_treeCanopyLoMat, &_treeCanopyHiMat,
                         _treeTrunks[i], _treeCanopyLo[i], _treeCanopyHi[i]);
            _scene->addObject(_treeTrunks[i]);
            _scene->addObject(_treeCanopyLo[i]);
            _scene->addObject(_treeCanopyHi[i]);
        }

        for (int i = 0; i < REPAIR_COUNT; ++i) {
            _kits[i].obj = Primitives::createCube(130, 130, 130, &_kitMat);
            _kits[i].obj->setPosition(REPAIRS[i].x, hillHeight(REPAIRS[i].x, REPAIRS[i].z) + 90,
                                      REPAIRS[i].z);
            _scene->addObject(_kits[i].obj);
        }

        // Enemy tanks: a low hull, a smaller turret box on top, and a gun
        // barrel — proportions loosely referenced from a Blockbench-style
        // low-poly tank model the user found (hull noticeably wider than
        // the turret, a thin barrel roughly half the hull's width long),
        // rebuilt from Jet's own primitives rather than imported: that
        // model was 872 triangles and textured, both well outside what
        // this hardware/JetConfig can use. createCylinder's segments
        // parameter drives BOTH its radial sides and its vertical bands
        // (checked directly in Primitives.cpp — they share one loop), so
        // segments=4 keeps it cheap (8 triangles) at the cost of a
        // slightly faceted barrel, unnoticeable at this resolution.
        // Radius bumped from a first-pass 12 to 20 after playtest feedback
        // that it "didn't look much different" — a true-to-scale barrel is
        // only 1-2 pixels wide on a 160x128 panel and all but disappears
        // regardless of colour. closedCaps=true (was false): an open tube
        // has nothing at the muzzle end, so head-on — the single most
        // common viewing angle, since enemies drive toward the player —
        // you looked straight through it at the turret behind, which is
        // exactly what playtest reported as "the barrel hole blends in."
        // +4 triangles for the two end caps at segments=4.
        for (int i = 0; i < MAX_ENEMIES; ++i) {
            _enemies[i].hull   = Primitives::createCube(280, 110, 380, &_enemyHullMat);
            _enemies[i].turret = Primitives::createCube(150, 90, 150, &_enemyTurretMat);
            _enemies[i].barrel = Primitives::createCylinder(20, BARREL_LENGTH, 4, true, &_enemyBarrelMat);
            _enemies[i].trackL = Primitives::createCube(TRACK_WIDTH, TRACK_HEIGHT, TRACK_DEPTH, &_enemyTrackMat);
            _enemies[i].trackR = Primitives::createCube(TRACK_WIDTH, TRACK_HEIGHT, TRACK_DEPTH, &_enemyTrackMat);
            _enemies[i].hull->enabled   = false;
            _enemies[i].turret->enabled = false;
            _enemies[i].barrel->enabled = false;
            _enemies[i].trackL->enabled = false;
            _enemies[i].trackR->enabled = false;
            _scene->addObject(_enemies[i].hull);
            _scene->addObject(_enemies[i].turret);
            _scene->addObject(_enemies[i].barrel);
            _scene->addObject(_enemies[i].trackL);
            _scene->addObject(_enemies[i].trackR);
        }

        // Boss: same construction as the regular tanks, at 1.6x every
        // dimension/offset, its own materials, and its own hp pool
        // (BOSS_HP) — see trySpawnBoss()/BOSS_EVERY_KILLS.
        _boss.hull   = Primitives::createCube(BOSS_HULL_W, BOSS_HULL_H, BOSS_HULL_D, &_bossHullMat);
        _boss.turret = Primitives::createCube(BOSS_TURRET_W, BOSS_TURRET_H, BOSS_TURRET_W, &_bossTurretMat);
        _boss.barrel = Primitives::createCylinder(BOSS_BARREL_R, BOSS_BARREL_LEN, 4, true, &_bossTurretMat);
        _boss.trackL = Primitives::createCube(BOSS_TRACK_W, BOSS_TRACK_H, BOSS_TRACK_D, &_enemyTrackMat);
        _boss.trackR = Primitives::createCube(BOSS_TRACK_W, BOSS_TRACK_H, BOSS_TRACK_D, &_enemyTrackMat);
        _boss.hull->enabled   = false;
        _boss.turret->enabled = false;
        _boss.barrel->enabled = false;
        _boss.trackL->enabled = false;
        _boss.trackR->enabled = false;
        _scene->addObject(_boss.hull);
        _scene->addObject(_boss.turret);
        _scene->addObject(_boss.barrel);
        _scene->addObject(_boss.trackL);
        _scene->addObject(_boss.trackR);

        _playerShell.obj = Primitives::createCube(46, 46, 46, &_playerShellMat);
        _playerShell.obj->enabled = false;
        _scene->addObject(_playerShell.obj);

        for (int i = 0; i < MAX_ENEMY_SHELLS; ++i) {
            _enemyShells[i].obj = Primitives::createCube(46, 46, 46, &_enemyShellMat);
            _enemyShells[i].obj->enabled = false;
            _scene->addObject(_enemyShells[i].obj);
        }
    }

    // Shortest signed difference between two headings, in degrees.
    static float angleDiff(float target, float current) {
        float d = target - current;
        while (d >  180.0f) d -= 360.0f;
        while (d < -180.0f) d += 360.0f;
        return d;
    }

    static float wrapAngle(float a) {
        while (a >= 360.0f) a -= 360.0f;
        while (a <    0.0f) a += 360.0f;
        return a;
    }

    // Heading that points from (fromX,fromZ) at (toX,toZ), matching this
    // game's forward convention of (sin(h), 0, cos(h)).
    static float bearingTo(float fromX, float fromZ, float toX, float toZ) {
        return degrees(atan2f(toX - fromX, toZ - fromZ));
    }

    bool blockedFor(float x, float z, int32_t radius) const {
        for (int i = 0; i < OBSTACLE_COUNT; ++i) {
            float dx = x - (float)OBSTACLES[i].x;
            float dz = z - (float)OBSTACLES[i].z;
            float r  = (float)(radius + obstacleRadius(OBSTACLES[i]));
            if (dx * dx + dz * dz < r * r) return true;
        }
        return false;
    }

    // Circle-vs-circle push-out, used only for the player's own movement.
    // The old axis-separated approach (reject the X move if blocked, reject
    // the Z move if blocked) could deadlock: driving nearly straight at an
    // obstacle makes BOTH the pure-X and pure-Z probe land inside it every
    // single frame, so neither axis ever moves and _speed just oscillates
    // near zero as SPEED_SMOOTH rebuilds it and the next frame's probes
    // knock it straight back down — "stuck against a low object" from
    // playtest. Pushing the candidate position back out to the obstacle's
    // boundary along the direction from its centre instead makes the tank
    // slide along whatever it's driving into, at any approach angle.
    void resolveObstacleCollision(float &x, float &z) const {
        for (int i = 0; i < OBSTACLE_COUNT; ++i) {
            float dx = x - (float)OBSTACLES[i].x;
            float dz = z - (float)OBSTACLES[i].z;
            float r  = (float)(TANK_RADIUS + obstacleRadius(OBSTACLES[i]));
            float d2 = dx * dx + dz * dz;
            if (d2 < r * r) {
                float d = sqrtf(d2);
                if (d < 0.0001f) { dx = r; dz = 0.0f; d = r; }  // exactly on centre
                float push = (r - d) / d;
                x += dx * push;
                z += dz * push;
            }
        }
    }

    static bool within(float ax, float az, float bx, float bz, int32_t radius) {
        float dx = ax - bx, dz = az - bz;
        return dx * dx + dz * dz < (float)radius * (float)radius;
    }

    void fireShell(Shell &s, float x, float z, float headingDeg, float speed) {
        float hr = radians(headingDeg);
        s.x = x;
        s.z = z;
        s.vx = sinf(hr) * speed;
        s.vz = cosf(hr) * speed;
        s.travelled = 0.0f;
        s.active = true;
        s.obj->enabled = true;
        s.obj->setPosition((int32_t)s.x, SHELL_Y, (int32_t)s.z);
    }

    void killShell(Shell &s) {
        s.active = false;
        s.obj->enabled = false;
    }

    // Advances a shell and returns true while it's still in flight. Shells
    // die on obstacles, which is what turns cover into actual cover — no
    // separate line-of-sight test is needed anywhere else.
    bool advanceShell(Shell &s, int32_t range) {
        s.x += s.vx;
        s.z += s.vz;
        s.travelled += sqrtf(s.vx * s.vx + s.vz * s.vz);
        s.obj->setPosition((int32_t)s.x, SHELL_Y, (int32_t)s.z);

        if (s.travelled > (float)range ||
            fabsf(s.x) > (float)ARENA_HALF || fabsf(s.z) > (float)ARENA_HALF) {
            killShell(s);
            return false;
        }
        if (blockedFor(s.x, s.z, 20)) {
            _particles.emitSparks(Renderer::Vec3f{ s.x, (float)SHELL_Y, s.z },
                                  Renderer::Vec3f{ 0, 1, 0 }, 260.0f, 8);
            killShell(s);
            return false;
        }
        return true;
    }

    void updateDriving(const InputState &input) {
        _headingDeg += TURN_SIGN * input.joyY * TURN_RATE;
        while (_headingDeg >= 360.0f) _headingDeg -= 360.0f;
        while (_headingDeg <    0.0f) _headingDeg += 360.0f;

        float headRad = radians(_headingDeg);
        float fx = sinf(headRad);
        float fz = cosf(headRad);

        float nx, nz;
        if (input.btnB) {
            // Strafe mode: joyX moves along the tank's right vector
            // (perpendicular to heading, same (cos h, -sin h) used for the
            // enemy track strips) instead of driving forward/back. Reuses
            // _speed/SPEED_SMOOTH so strafing eases in/out exactly like
            // normal driving does, just along a different axis.
            float strafeDrive = STRAFE_SIGN * input.joyX;
            float target = strafeDrive * STRAFE_SPEED;
            _speed += (target - _speed) * SPEED_SMOOTH;
            float rx = fz, rz = -fx;
            nx = _x + rx * _speed;
            nz = _z + rz * _speed;
        } else {
            float drive  = DRIVE_SIGN * input.joyX;
            float target = drive * (drive >= 0.0f ? FWD_SPEED : REV_SPEED);
            _speed += (target - _speed) * SPEED_SMOOTH;
            nx = _x + fx * _speed;
            nz = _z + fz * _speed;
        }
        resolveObstacleCollision(nx, nz);
        _x = nx;
        _z = nz;

        const float limit = (float)(ARENA_HALF - TANK_RADIUS);
        _x = constrain(_x, -limit, limit);
        _z = constrain(_z, -limit, limit);

        _camera.setPosition((int32_t)_x, EYE_HEIGHT, (int32_t)_z);
        _camera.setRotation(0, (int32_t)_headingDeg, 0);
    }

    // How many tanks may be on the field at once. Ramps 1 -> 2 -> 3 so the
    // opening is survivable while you learn where cover is.
    int enemyCap() const {
        if (_level <= 2) return 1;
        if (_level <= 4) return 2;
        return MAX_ENEMIES;
    }

    int aliveEnemies() const {
        int n = 0;
        for (const auto &e : _enemies) if (e.alive) ++n;
        return n;
    }

    float enemySpeed() const {
        return ENEMY_SPEED + (float)(_level - 1) * 0.9f;
    }

    unsigned long fireDelay() const {
        unsigned long cut = (unsigned long)(_level - 1) * 320;
        unsigned long lo = (ENEMY_FIRE_MIN_MS > cut + ENEMY_FIRE_FLOOR_MS)
                         ? ENEMY_FIRE_MIN_MS - cut : ENEMY_FIRE_FLOOR_MS;
        unsigned long hi = (ENEMY_FIRE_MAX_MS > cut + ENEMY_FIRE_FLOOR_MS)
                         ? ENEMY_FIRE_MAX_MS - cut : ENEMY_FIRE_FLOOR_MS + 800;
        return millis() + (unsigned long)random((long)lo, (long)hi);
    }

    // Class only enters the mix once _level already puts more than one
    // tank on the field (see enemyCap()) — the opening stays exactly the
    // single-class fight it always was.
    EnemyClass pickEnemyClass() const {
        if (_level <= 2) return CLASS_1;
        if (_level <= 4) return (random(0, 2) == 0) ? CLASS_1 : CLASS_2;
        int r = random(0, 3);
        return r == 0 ? CLASS_1 : (r == 1 ? CLASS_2 : CLASS_3);
    }

    // Repoints every triangle of an already-built mesh at a different
    // material — cheap (Triangle::material is a public pointer, no
    // geometry rebuild) and only ever called once per spawn, not per
    // frame. Used to recolour a pooled enemy's turret by class without
    // needing separate geometry per class.
    static void setObjectMaterial(Renderer::Object* obj, Renderer::Material* mat) {
        for (auto &tri : obj->triangles) tri.material = mat;
    }

    void spawnEnemy(Enemy &e) {
        // Spawn out on the perimeter, and not right on top of the player.
        for (int attempt = 0; attempt < 12; ++attempt) {
            float ang = radians((float)random(0, 360));
            float r   = (float)(ARENA_HALF - 400);
            float ex  = sinf(ang) * r;
            float ez  = cosf(ang) * r;
            if (blockedFor(ex, ez, ENEMY_RADIUS)) continue;
            if (within(ex, ez, _x, _z, 1600)) continue;
            e.x = ex;
            e.z = ez;
            e.headingDeg = bearingTo(ex, ez, _x, _z);
            e.alive = true;
            e.tankClass = pickEnemyClass();
            e.hp = e.maxHp = CLASS_HP[e.tankClass];
            setObjectMaterial(e.turret, e.tankClass == CLASS_1 ? &_enemyTurretMat
                                       : e.tankClass == CLASS_2 ? &_enemyTurretMatClass2
                                                                 : &_enemyTurretMatClass3);
            e.hull->enabled = true;
            e.turret->enabled = true;
            e.barrel->enabled = true;
            e.trackL->enabled = true;
            e.trackR->enabled = true;
            e.nextFireAt = fireDelay();
            return;
        }
        // Every candidate was blocked or too close; try again next frame.
        e.respawnAt = millis() + 400;
    }

    // A hit that doesn't kill: multi-hp classes and the boss both need
    // this now, so it's split out from destroyEnemy() rather than folded
    // back in — a smaller spark burst and a plain clang tone (distinct
    // from tryFire()'s 950Hz shot and the explosion.wav a kill gets), so
    // "that connected but didn't finish it" reads differently from both.
    void hitEnemy(Enemy &e, AudioEngine &audio) {
        e.hp--;
        if (e.hp <= 0) {
            destroyEnemy(e, audio);
            return;
        }
        _particles.emitSparks(Renderer::Vec3f{ e.x, 120.0f, e.z },
                              Renderer::Vec3f{ 0, 1, 0 }, 300.0f, 10);
        audio.playTone(650, 50);
    }

    void destroyEnemy(Enemy &e, AudioEngine &audio) {
        bool isBoss = (&e == &_boss);
        _particles.emitSparks(Renderer::Vec3f{ e.x, 120.0f, e.z },
                              Renderer::Vec3f{ 0, 1, 0 }, 520.0f, isBoss ? 46 : 26);
        e.alive = false;
        e.hull->enabled = false;
        e.turret->enabled = false;
        e.barrel->enabled = false;
        e.trackL->enabled = false;
        e.trackR->enabled = false;
        // Shared explosion asset (SharedAssets.h) — same /audio/explosion.wav
        // AsteroidFlux and LanderFlux already use, with the same PROGMEM
        // fallback. Firing keeps its own short tone (tryFire()'s 950Hz
        // blip, enemy fire's 420Hz one): a shot igniting and a shell
        // detonating are different events and shouldn't sound the same.
        audio.playExplosionSound(explosion_data, sizeof(explosion_data));

        if (isBoss) {
            // Doesn't touch _kills/_level: the boss is a detour from the
            // regular escalation, not a step in it, and counting its own
            // death toward _kills risks it landing on another multiple of
            // BOSS_EVERY_KILLS and re-triggering itself immediately.
            _bossActive = false;
            _score += BOSS_SCORE;
            audio.playTone(1900, 300);   // bigger fanfare than the regular level-up cue
            return;
        }

        e.respawnAt = millis() + ENEMY_RESPAWN_MS;
        _score += SCORE_PER_KILL;
        _kills++;
        int newLevel = min(MAX_LEVEL, 1 + _kills / KILLS_PER_LEVEL);
        if (newLevel != _level) {
            _level = newLevel;
            audio.playTone(1900, 140);   // level-up cue
        }
        if (!_bossActive && _kills >= _nextBossAt) {
            _nextBossAt += BOSS_EVERY_KILLS;
            _bossPending = true;
        }
    }

    // Returns false (and leaves _bossPending set) if every perimeter
    // candidate was blocked — updateEnemies() retries next frame, the
    // same pattern spawnEnemy() uses for the regular pool.
    bool trySpawnBoss(AudioEngine &audio) {
        // Always on the far side of the arena from the player, not just
        // "reject if too close": a pure random-angle-plus-reject approach
        // (what the regular spawnEnemy() perimeter search does) can still
        // land within a couple hundred units of the player by chance —
        // "scary when he's right behind you" from playtest, a boss should
        // be seen coming. Base angle is the player's own angle from the
        // arena centre plus 180°, with modest jitter so it isn't perfectly
        // predictable every time. This is also the only way to GUARANTEE
        // separation regardless of where the player is standing: a fixed
        // minimum-distance floor would be impossible to satisfy (and loop
        // forever) whenever the player is near the arena centre, since
        // every point on the spawn circle is then roughly the same
        // distance away.
        float playerAngle = atan2f(_x, _z);   // matches bearingTo()'s atan2(dx,dz) convention
        for (int attempt = 0; attempt < 12; ++attempt) {
            float jitter = radians((float)random(-BOSS_SPAWN_JITTER_DEG, BOSS_SPAWN_JITTER_DEG + 1));
            float ang = playerAngle + PI + jitter;
            float r   = (float)(ARENA_HALF - 500);
            float ex  = sinf(ang) * r;
            float ez  = cosf(ang) * r;
            if (blockedFor(ex, ez, BOSS_RADIUS)) continue;
            if (within(ex, ez, _x, _z, BOSS_MIN_SPAWN_DIST)) continue;
            _boss.x = ex;
            _boss.z = ez;
            _boss.headingDeg = bearingTo(ex, ez, _x, _z);
            _boss.alive = true;
            _boss.hp = _boss.maxHp = BOSS_HP;
            _boss.hull->enabled   = true;
            _boss.turret->enabled = true;
            _boss.barrel->enabled = true;
            _boss.trackL->enabled = true;
            _boss.trackR->enabled = true;
            _boss.nextFireAt = fireDelay();
            _bossActive = true;
            audio.playTone(300, 400);   // low arrival cue, distinct from the kill fanfare
            return true;
        }
        return false;
    }

    // Chase/aim/fire — identical for the regular pool and the boss, so
    // both call this rather than duplicating it. Mesh placement is NOT
    // handled here since the boss uses different offsets for its larger
    // geometry; see updateEnemyTransform()/updateBossTransform().
    void updateEnemyAI(Enemy &e, float speed, AudioEngine &audio) {
        float want = bearingTo(e.x, e.z, _x, _z);
        float err  = angleDiff(want, e.headingDeg);
        e.headingDeg = wrapAngle(e.headingDeg +
                                 constrain(err, -ENEMY_TURN_RATE, ENEMY_TURN_RATE));

        float dx = _x - e.x, dz = _z - e.z;
        float dist = sqrtf(dx * dx + dz * dz);

        if (dist > (float)ENEMY_STANDOFF) {
            float hr = radians(e.headingDeg);
            float nx = e.x + sinf(hr) * speed;
            float nz = e.z + cosf(hr) * speed;
            if (!blockedFor(nx, nz, ENEMY_RADIUS)) {
                e.x = nx;
                e.z = nz;
            } else {
                // Scrape around whatever it walked into rather than
                // grinding against it forever.
                e.headingDeg = wrapAngle(e.headingDeg + 9.0f);
            }
            const float limit = (float)(ARENA_HALF - ENEMY_RADIUS);
            e.x = constrain(e.x, -limit, limit);
            e.z = constrain(e.z, -limit, limit);
        }

        if (fabsf(err) < ENEMY_AIM_TOLERANCE && dist < (float)ENEMY_FIRE_RANGE &&
            (long)(millis() - e.nextFireAt) >= 0) {
            for (auto &s : _enemyShells) {
                if (s.active) continue;
                float hr = radians(e.headingDeg);
                fireShell(s, e.x + sinf(hr) * 220.0f, e.z + cosf(hr) * 220.0f,
                          e.headingDeg, ENEMY_SHELL_SPEED);
                // Shared /audio/shot.wav — same asset and call as the
                // player's own tryFire(). playWAV() stops whatever's
                // currently playing first (same as playExplosionSound()),
                // so a shot fired right after a kill can cut its
                // explosion.wav short; accepted, matches how every other
                // sound in this single-channel setup already behaves.
                audio.playWAV("/audio/shot.wav");
                break;
            }
            e.nextFireAt = fireDelay();
        }
    }

    // createCylinder's local axis is Y; rotX=90 tips it onto its side, and
    // Object's actual composition order (checked in Scene.cpp: M = Rz*Ry*Rx,
    // applied to local vertices — NOT the Y-then-X order
    // Camera::transformDirection uses for a different purpose, confirmed by
    // hand rather than assumed from that comment) then makes rotY=heading
    // aim the now-horizontal barrel down (sin(heading), 0, cos(heading)) —
    // this game's own forward convention everywhere else.
    void updateEnemyTransform(Enemy &e) {
        e.hull->setPosition((int32_t)e.x, 55, (int32_t)e.z);
        e.hull->setRotation(0, (int32_t)e.headingDeg, 0);
        e.turret->setPosition((int32_t)e.x, 155, (int32_t)e.z);
        e.turret->setRotation(0, (int32_t)e.headingDeg, 0);

        // Offset forward by half its length so it reads as bolted to the
        // turret's front rather than centred through it.
        float barrelHr = radians(e.headingDeg);
        int32_t bx = (int32_t)(e.x + sinf(barrelHr) * (float)(BARREL_LENGTH / 2));
        int32_t bz = (int32_t)(e.z + cosf(barrelHr) * (float)(BARREL_LENGTH / 2));
        e.barrel->setPosition(bx, 155, bz);
        e.barrel->setRotation(90, (int32_t)e.headingDeg, 0);

        // Track strips: offset sideways from the hull centre along the
        // vector perpendicular to heading (sin h, cos h) — (cos h, -sin h)
        // — at a lower Y so they read as a base the hull sits on.
        int32_t rx = (int32_t)(cosf(barrelHr) * (float)TRACK_OFFSET);
        int32_t rz = (int32_t)(-sinf(barrelHr) * (float)TRACK_OFFSET);
        e.trackL->setPosition((int32_t)e.x - rx, 30, (int32_t)e.z - rz);
        e.trackL->setRotation(0, (int32_t)e.headingDeg, 0);
        e.trackR->setPosition((int32_t)e.x + rx, 30, (int32_t)e.z + rz);
        e.trackR->setRotation(0, (int32_t)e.headingDeg, 0);
    }

    // Same placement logic as updateEnemyTransform(), at the boss's own
    // 1.6x dimensions/offsets/Y-heights instead of the regular tank's.
    void updateBossTransform() {
        Enemy &e = _boss;
        e.hull->setPosition((int32_t)e.x, BOSS_HULL_Y, (int32_t)e.z);
        e.hull->setRotation(0, (int32_t)e.headingDeg, 0);
        e.turret->setPosition((int32_t)e.x, BOSS_TURRET_Y, (int32_t)e.z);
        e.turret->setRotation(0, (int32_t)e.headingDeg, 0);

        float barrelHr = radians(e.headingDeg);
        int32_t bx = (int32_t)(e.x + sinf(barrelHr) * (float)(BOSS_BARREL_LEN / 2));
        int32_t bz = (int32_t)(e.z + cosf(barrelHr) * (float)(BOSS_BARREL_LEN / 2));
        e.barrel->setPosition(bx, BOSS_TURRET_Y, bz);
        e.barrel->setRotation(90, (int32_t)e.headingDeg, 0);

        int32_t rx = (int32_t)(cosf(barrelHr) * (float)BOSS_TRACK_OFFSET);
        int32_t rz = (int32_t)(-sinf(barrelHr) * (float)BOSS_TRACK_OFFSET);
        e.trackL->setPosition((int32_t)e.x - rx, BOSS_TRACK_Y, (int32_t)e.z - rz);
        e.trackL->setRotation(0, (int32_t)e.headingDeg, 0);
        e.trackR->setPosition((int32_t)e.x + rx, BOSS_TRACK_Y, (int32_t)e.z + rz);
        e.trackR->setRotation(0, (int32_t)e.headingDeg, 0);
    }

    void updateEnemies(AudioEngine &audio) {
        for (auto &e : _enemies) {
            if (!e.alive) {
                // Boss fights don't backfill the regular pool — the field
                // stays boss-only (plus any stragglers already alive when
                // it triggered) until it's dead.
                if (_bossActive) continue;
                // Hold the slot shut while at cap, pushing the timer along so
                // a kill always buys a breather rather than being replaced
                // the same instant.
                if (aliveEnemies() >= enemyCap()) {
                    e.respawnAt = millis() + ENEMY_RESPAWN_MS;
                } else if ((long)(millis() - e.respawnAt) >= 0) {
                    spawnEnemy(e);
                }
                continue;
            }
            updateEnemyAI(e, enemySpeed(), audio);
            updateEnemyTransform(e);
        }

        if (_bossActive) {
            updateEnemyAI(_boss, enemySpeed(), audio);
            updateBossTransform();
        } else if (_bossPending) {
            if (trySpawnBoss(audio)) _bossPending = false;
        }
    }

    void updateShells(AudioEngine &audio) {
        if (_playerShell.active && advanceShell(_playerShell, PLAYER_SHELL_RANGE)) {
            bool hit = false;
            for (auto &e : _enemies) {
                if (!e.alive) continue;
                if (within(_playerShell.x, _playerShell.z, e.x, e.z, KILL_RADIUS)) {
                    hitEnemy(e, audio);
                    killShell(_playerShell);
                    hit = true;
                    break;
                }
            }
            if (!hit && _bossActive &&
                within(_playerShell.x, _playerShell.z, _boss.x, _boss.z, BOSS_KILL_RADIUS)) {
                hitEnemy(_boss, audio);
                killShell(_playerShell);
            }
        }

        for (auto &s : _enemyShells) {
            if (!s.active) continue;
            if (!advanceShell(s, ENEMY_SHELL_RANGE)) continue;
            if (within(s.x, s.z, _x, _z, HIT_RADIUS)) {
                // Player-hit feedback: sparks and the same shared explosion
                // sound as destroyEnemy() right at the impact point,
                // complementing the existing screen flash. advanceShell()
                // already covers shell-vs-obstacle; this was the one impact
                // case with no particles or explosion audio at all.
                _particles.emitSparks(Renderer::Vec3f{ s.x, (float)SHELL_Y, s.z },
                                      Renderer::Vec3f{ 0, 1, 0 }, 300.0f, 16);
                _health -= HIT_DAMAGE;
                _damageFlashUntil = millis() + 160;
                killShell(s);
                audio.playExplosionSound(explosion_data, sizeof(explosion_data));
            }
        }
    }

    void tryFire(const InputState &input, AudioEngine &audio) {
        if (!input.btnAPressed) return;
        if (_playerShell.active) return;              // one shell in flight
        if ((long)(millis() - _reloadAt) < 0) return;

        float hr = radians(_headingDeg);
        fireShell(_playerShell,
                  _x + sinf(hr) * 200.0f, _z + cosf(hr) * 200.0f,
                  _headingDeg, PLAYER_SHELL_SPEED);
        _reloadAt = millis() + PLAYER_RELOAD_MS;
        _muzzleFlashUntil = millis() + 70;
        audio.playWAV("/audio/shot.wav");
    }

    void updateKits(AudioEngine &audio) {
        for (int i = 0; i < REPAIR_COUNT; ++i) {
            RepairKit &k = _kits[i];
            if (!k.active) {
                if ((long)(millis() - k.respawnAt) >= 0) {
                    k.active = true;
                    k.obj->enabled = true;
                }
                continue;
            }
            k.obj->rotate(0, 3, 0);   // slow spin, so pickups read as pickups

            // Picked up whenever you drive through, even at full health —
            // a kit that silently refuses to collect reads as a bug.
            float dx = _x - (float)REPAIRS[i].x;
            float dz = _z - (float)REPAIRS[i].z;
            if (dx * dx + dz * dz < (float)REPAIR_PICKUP_RADIUS * REPAIR_PICKUP_RADIUS) {
                _health = min(HEALTH_MAX, _health + REPAIR_AMOUNT);
                k.active = false;
                k.obj->enabled = false;
                k.respawnAt = millis() + REPAIR_RESPAWN_MS;
                audio.playWAV("/audio/repair.wav");
            }
        }
    }

    void startNewGame(AudioEngine &audio) {
        audio.stopLoop();   // same call AsteroidFluxGame uses to end its attract loop
        _x = 0.0f;
        _z = 0.0f;
        _headingDeg = 0.0f;
        _speed = 0.0f;
        _health = HEALTH_MAX;
        _score = 0;
        _kills = 0;
        _level = 1;
        _reloadAt = 0;
        _muzzleFlashUntil = 0;
        _damageFlashUntil = 0;
        for (int i = 0; i < REPAIR_COUNT; ++i) {
            _kits[i].active = true;
            if (_kits[i].obj) _kits[i].obj->enabled = true;
        }
        killShell(_playerShell);
        for (auto &s : _enemyShells) killShell(s);
        for (int i = 0; i < MAX_ENEMIES; ++i) {
            _enemies[i].alive = false;
            _enemies[i].hull->enabled   = false;
            _enemies[i].turret->enabled = false;
            _enemies[i].barrel->enabled = false;
            _enemies[i].trackL->enabled = false;
            _enemies[i].trackR->enabled = false;
            // enemyCap() holds the extras back at level 1 regardless; this
            // just gives the first arrival a moment's grace.
            _enemies[i].respawnAt = millis() + 1200;
        }
        _boss.alive = false;
        _boss.hull->enabled   = false;
        _boss.turret->enabled = false;
        _boss.barrel->enabled = false;
        _boss.trackL->enabled = false;
        _boss.trackR->enabled = false;
        _bossActive  = false;
        _bossPending = false;
        _nextBossAt  = BOSS_EVERY_KILLS;
        for (auto &p : _particles.pool) p.active = false;
        _phase = PHASE_PLAYING;
        audio.playTone(900, 80);
    }

    void drawHUD(GFXcanvas16 &canvas) {
        canvas.fillRect(0, 0, ArcadeConfig::LANDSCAPE_WIDTH, 10, ArcadeConfig::COLOR_BLACK);
        canvas.drawFastHLine(0, 10, ArcadeConfig::LANDSCAPE_WIDTH, ArcadeConfig::COLOR_GREEN);

        canvas.setFont();
        canvas.setTextSize(1);
        canvas.setTextColor(ArcadeConfig::COLOR_YELLOW);
        canvas.setCursor(4, 1);
        canvas.print("SCORE:"); canvas.print(_score);

        canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
        canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH / 2 - 6, 1);
        canvas.print("LV"); canvas.print(_level);

        canvas.setTextColor(ArcadeConfig::COLOR_GREY);
        canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH - 54, 1);
        canvas.print("HI:"); canvas.print(_highScore);

        // Boss health bar: a second, wider bar directly under the main HUD
        // strip, only while a boss fight is on. Red so it reads as "the
        // threat," distinct from the player's own green/amber/red bar.
        if (_bossActive) {
            const int bx = 3, by = 13, bw = ArcadeConfig::LANDSCAPE_WIDTH - 6, bh = 6;
            canvas.drawRect(bx, by, bw, bh, ArcadeConfig::COLOR_GREY);
            int bossFillW = ((bw - 2) * _boss.hp) / max(1, _boss.maxHp);
            if (bossFillW > 0) {
                canvas.fillRect(bx + 1, by + 1, bossFillW, bh - 2, ArcadeConfig::COLOR_RED);
            }
        }

        // Health bar sits bottom-left, stopping short of centre so it doesn't
        // run across the gun barrel.
        const int barX = 3, barY = ArcadeConfig::LANDSCAPE_HEIGHT - 7;
        const int barW = 58, barH = 5;
        canvas.drawRect(barX, barY, barW, barH, ArcadeConfig::COLOR_GREY);
        int fillW = ((barW - 2) * _health) / HEALTH_MAX;
        if (fillW > 0) {
            uint16_t c = (_health > 60) ? ArcadeConfig::COLOR_GREEN
                       : (_health > 30) ? ArcadeConfig::COLOR_AMBER
                                        : ArcadeConfig::COLOR_RED;
            canvas.fillRect(barX + 1, barY + 1, fillW, barH - 2, c);
        }
    }

    // A visible sun disc, projected from the same _sun direction that
    // actually lights the world — same idea as the LensFlare in Jet's
    // Tropical Island example, but drawn directly with plain 2D circles
    // (matching the muzzle-flash pattern just below) rather than pulling in
    // Jet's Sprite2D/LensFlare/pick-query machinery: this project doesn't
    // enable TEXTURE_MAPPING or MAX_PICK_QUERIES, and a single fixed light
    // never needs occlusion fading or a multi-element flare chain. Drawn
    // straight onto the canvas after the 3D scene, so it paints over the
    // sky gradient — clipped to stay above the horizon so it can't paint
    // over ground geometry it should logically be behind.
    void drawSun(GFXcanvas16 &canvas) {
        Vector3 viewDir = _camera.transformDirection(_sunVisual.worldLightDir);
        if (viewDir.z <= 0) return;   // behind the camera
        const float invZ = _camera.fovFactor / (float)viewDir.z;
        const int sx = canvas.width()  / 2 + (int)(viewDir.x * invZ);
        const int sy = canvas.height() / 2 - (int)(viewDir.y * invZ);
        if (sy >= canvas.height() / 2 - 6) return;   // at/below the horizon
        if (sy < 12) return;   // drawHUD's top bar (rows 0-9) paints over this anyway
        if (sx < -20 || sx > canvas.width() + 20) return;
        canvas.fillCircle(sx, sy, 9, rgb565(31, 55, 24));   // pale halo
        canvas.fillCircle(sx, sy, 5, rgb565(31, 50, 10));   // warm core
        canvas.fillCircle(sx, sy, 2, ArcadeConfig::COLOR_WHITE);
    }

    void drawGunsight(GFXcanvas16 &canvas, int cx, int cy) {
        canvas.drawFastHLine(cx - 8, cy, 6, ArcadeConfig::COLOR_GREEN);
        canvas.drawFastHLine(cx + 3, cy, 6, ArcadeConfig::COLOR_GREEN);
        canvas.drawFastVLine(cx, cy - 8, 6, ArcadeConfig::COLOR_GREEN);
        canvas.drawFastVLine(cx, cy + 3, 6, ArcadeConfig::COLOR_GREEN);
    }

    // The barrel is drawn in 2D rather than as a 3D object because it's
    // rigidly bolted to the vehicle the camera sits inside — it should never
    // move relative to the screen, so a screen-space shape is both simpler
    // and strictly more correct than positioning a mesh in front of the
    // camera every frame.
    void drawBarrel(GFXcanvas16 &canvas) {
        const int cx = canvas.width() / 2;
        const int base = canvas.height() - 1;
        const int tipY = canvas.height() - 34;
        const uint16_t body = rgb565(9, 18, 10);
        const uint16_t edge = rgb565(16, 32, 18);

        canvas.fillTriangle(cx - 13, base, cx + 13, base, cx + 6, tipY, body);
        canvas.fillTriangle(cx - 13, base, cx + 6, tipY, cx - 6, tipY, body);
        canvas.drawLine(cx - 13, base, cx - 6, tipY, edge);
        canvas.drawLine(cx + 13, base, cx + 6, tipY, edge);
        canvas.drawFastHLine(cx - 6, tipY, 13, edge);

        if ((long)(_muzzleFlashUntil - millis()) > 0) {
            canvas.fillCircle(cx, tipY - 2, 5, ArcadeConfig::COLOR_YELLOW);
            canvas.fillCircle(cx, tipY - 2, 2, ArcadeConfig::COLOR_WHITE);
        }
    }

    // Rotated radar: forward is always up, so a blip's position on the dial
    // is the direction you need to turn. Without this, an attacker that
    // slides out of frame is simply lost — which is the single hardest
    // thing to deal with in a first-person game on a 160x128 screen.
    // Shows the whole arena, deliberately beyond visual range, so shells
    // arriving out of the haze still have a visible source.
    void drawRadar(GFXcanvas16 &canvas) {
        const int r  = 20;
        const int cx = canvas.width() - r - 5;
        const int cy = canvas.height() - r - 5;
        const float scale = (float)r / (float)RADAR_RANGE;

        canvas.fillCircle(cx, cy, r, rgb565(2, 6, 4));
        canvas.drawCircle(cx, cy, r, ArcadeConfig::COLOR_GREY);
        // Bow marker, so "up" is unambiguous.
        canvas.drawFastVLine(cx, cy - r, 4, ArcadeConfig::COLOR_GREY);

        const float hr = radians(_headingDeg);
        const float sh = sinf(hr), ch = cosf(hr);

        auto plot = [&](float wx, float wz, uint16_t colour, bool big) {
            float dx = wx - _x, dz = wz - _z;
            // Into tank-local space: forward = (sin h, cos h), right = (cos h, -sin h).
            float right   = dx * ch - dz * sh;
            float forward = dx * sh + dz * ch;
            float px = right * scale;
            float py = -forward * scale;
            // Clamp to the rim rather than dropping it, so something out of
            // range still tells you which way it is.
            float len = sqrtf(px * px + py * py);
            if (len > (float)(r - 2)) {
                float k = (float)(r - 2) / len;
                px *= k;
                py *= k;
            }
            int sx = cx + (int)px, sy = cy + (int)py;
            if (big) canvas.fillCircle(sx, sy, 2, colour);
            else     canvas.drawPixel(sx, sy, colour);
        };

        for (int i = 0; i < REPAIR_COUNT; ++i) {
            if (_kits[i].active) plot((float)REPAIRS[i].x, (float)REPAIRS[i].z,
                                      ArcadeConfig::COLOR_GREEN, false);
        }
        for (const auto &e : _enemies) {
            if (e.alive) plot(e.x, e.z, ArcadeConfig::COLOR_RED, true);
        }

        canvas.drawPixel(cx, cy, ArcadeConfig::COLOR_WHITE);
    }

    // A red border rather than a full-screen tint: at 160x128 a full flash
    // hides the very thing you need to see after being hit.
    void drawDamageFlash(GFXcanvas16 &canvas) {
        if ((long)(_damageFlashUntil - millis()) <= 0) return;
        const int w = canvas.width(), h = canvas.height();
        for (int i = 0; i < 3; ++i) {
            canvas.drawRect(i, 11 + i, w - i * 2, h - 11 - i * 2, ArcadeConfig::COLOR_RED);
        }
    }

    // Real title art (assets/TitleScreen.h) replaces the old placeholder
    // (a code-drawn tank icon + "TANK FLUX" text) now that it exists —
    // same full-screen PROGMEM blit every other game's splash uses. The
    // source image's own bottom 20px are solid black, left there
    // deliberately for this HUD text: start prompt and high score,
    // centred, blinking prompt at the same 600ms-on/400ms-off cadence
    // Maze/Lander's own splash screens use.
    void renderAttractGame(GFXcanvas16 &canvas) {
        for (int i = 0; i < (ArcadeConfig::LANDSCAPE_WIDTH * ArcadeConfig::LANDSCAPE_HEIGHT); i++) {
            uint16_t px = pgm_read_word(&tank_flux_160x128_data[i]);
            canvas.drawPixel(i % ArcadeConfig::LANDSCAPE_WIDTH,
                             i / ArcadeConfig::LANDSCAPE_WIDTH, px);
        }

        canvas.setFont();
        canvas.setTextSize(1);
        int16_t tbx, tby; uint16_t tbw, tbh;

        if (millis() % 1000 < 600) {
            const char* prompt = "[BTN A] TO START";
            canvas.getTextBounds(prompt, 0, 0, &tbx, &tby, &tbw, &tbh);
            canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
            canvas.setCursor((ArcadeConfig::LANDSCAPE_WIDTH - (int16_t)tbw) / 2, 111);
            canvas.print(prompt);
        }

        char hiBuf[20];
        snprintf(hiBuf, sizeof(hiBuf), "HI: %d", _highScore);
        canvas.getTextBounds(hiBuf, 0, 0, &tbx, &tby, &tbw, &tbh);
        canvas.setTextColor(ArcadeConfig::COLOR_YELLOW);
        canvas.setCursor((ArcadeConfig::LANDSCAPE_WIDTH - (int16_t)tbw) / 2, 120);
        canvas.print(hiBuf);
    }

    // Updated for: strafe (BTN B now moves, doesn't quit, during PLAYING —
    // see updateDriving()/the top of update()), green (not white) repair
    // kits, and the boss. Ten more content lines than the version this
    // replaced had to fit in the same 128px height, so pitch is tightened
    // to 10px between lines (still setTextSize(1), the smallest built-in
    // font available here — there's no separate "small font" to switch
    // to) rather than dropping any line.
    void renderAttractInfo(GFXcanvas16 &canvas) {
        canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
        canvas.setFont();
        canvas.setTextSize(1);

        canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
        canvas.setCursor(30, 4);
        canvas.print("HOW TO PLAY");

        canvas.setTextColor(ArcadeConfig::COLOR_GREY);
        canvas.setCursor(4, 20);
        canvas.print("[JOY]   DRIVE / TURN");
        canvas.setCursor(4, 30);
        canvas.print("[BTN A] FIRE (1 SHELL)");
        canvas.setCursor(4, 40);
        canvas.print("[HOLD B]+JOY STRAFE");

        canvas.setTextColor(ArcadeConfig::COLOR_GREEN);
        canvas.setCursor(4, 56);
        canvas.print("GREEN CUBES = REPAIR");
        canvas.setTextColor(ArcadeConfig::COLOR_RED);
        canvas.setCursor(4, 66);
        canvas.print("RED DOTS ON RADAR = FOES");

        canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
        canvas.setCursor(4, 82);
        canvas.print("MORE TANKS EVERY FEW");
        canvas.setCursor(4, 92);
        canvas.print("KILLS -- SURVIVE!");
        canvas.setCursor(4, 102);
        canvas.print("BOSS TANK EVERY 15 KILLS");

        canvas.setTextColor(ArcadeConfig::COLOR_GREY);
        canvas.setCursor(28, 118);
        canvas.print("BEST: "); canvas.print(_highScore);
    }

public:
    TankFluxGame() {}

    void init(AudioEngine &audio) override {
        loadHighScore();
        _phase = PHASE_ATTRACT;
        _attractSlide      = SLIDE_GAME;
        _attractSlideTimer = millis();
        _attractMusicStarted = false;
        _btnBWasHeld = true;
        // Same convention as LanderFluxGame's playLanderStartSound(): try
        // /audio/tank_start.wav on SD first, else fall back to a short
        // generated melody (no PROGMEM sample needed).
        audio.playTankStartSound();
    }

    bool update(GFXcanvas16 &canvas,
                const InputState &input,
                AudioEngine &audio) override {
        ensureSceneReady(canvas);

        // --- Button B: require release first, then hold 2s to exit ---
        // Skipped during PHASE_PLAYING: holding B there strafes instead
        // (see updateDriving()), and this 2s hold-to-exit would otherwise
        // yank the player to the launcher mid-strafe. Still available from
        // ATTRACT to back out before starting; GAMEOVER already has its
        // own explicit single-press "[BTN B] QUIT".
        static unsigned long btnBHoldStart = 0;
        if (_phase != PHASE_PLAYING) {
            if (_btnBWasHeld) {
                if (!input.btnB) _btnBWasHeld = false;
            } else if (input.btnB) {
                if (btnBHoldStart == 0) btnBHoldStart = millis();
                if (millis() - btnBHoldStart > 2000) {
                    btnBHoldStart = 0;
                    audio.mute();
                    return false;
                }
            } else {
                btnBHoldStart = 0;
            }
        }

        // ---- PHASE: ATTRACT ----
        if (_phase == PHASE_ATTRACT) {
            // Loop the attract music once, guarded so it isn't re-issued
            // every frame. loopWAV() unconditionally stops whatever's
            // currently playing, so without these guards the startup sound
            // from init() — playTankStartSound() plays either a WAV
            // (isSamplePlaying()) or the fallback melody (isMelodyPlaying())
            // — would be cut off within one frame of ATTRACT starting.
            if (!_attractMusicStarted && !audio.isSamplePlaying() && !audio.isMelodyPlaying()) {
                audio.loopWAV("/audio/tank_loop.wav");
                _attractMusicStarted = true;
            }

            if (millis() - _attractSlideTimer > 8000) {
                _attractSlide      = (_attractSlide == SLIDE_GAME) ? SLIDE_INFO : SLIDE_GAME;
                _attractSlideTimer = millis();
            }

            if (_attractSlide == SLIDE_GAME) renderAttractGame(canvas);
            else                              renderAttractInfo(canvas);

            if (input.btnBPressed) {
                audio.mute();
                return false;
            }
            if (input.btnAPressed) startNewGame(audio);
            return true;
        }

        // ---- PHASE: GAME OVER ----
        if (_phase == PHASE_GAMEOVER) {
            canvas.fillScreen(ArcadeConfig::COLOR_BLACK);
            canvas.setTextColor(ArcadeConfig::COLOR_RED);
            canvas.setTextSize(2);
            canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH / 4 - 12, 15);
            canvas.print("DESTROYED");

            canvas.setTextSize(1);
            canvas.setTextColor(ArcadeConfig::COLOR_WHITE);
            canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH / 4, 45);
            canvas.print("SCORE: "); canvas.print(_score);

            if (_score >= _highScore && _score > 0) {
                canvas.setTextColor(ArcadeConfig::COLOR_GREEN);
                canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH / 4, 65);
                canvas.print("NEW HIGH SCORE!!");
            } else {
                canvas.setTextColor(ArcadeConfig::COLOR_GREY);
                canvas.setCursor(ArcadeConfig::LANDSCAPE_WIDTH / 4, 65);
                canvas.print("BEST: "); canvas.print(_highScore);
            }

            canvas.setTextColor(ArcadeConfig::COLOR_CYAN);
            canvas.setCursor(20, 90);
            canvas.print("[BTN A] PLAY AGAIN");
            canvas.setCursor(20, 103);
            canvas.print("[BTN B] QUIT");

            unsigned long elapsed = millis() - _gameOverEnteredMs;
            if (input.btnAPressed) { startNewGame(audio); return true; }
            if (input.btnBPressed) { audio.mute(); return false; }
            if (elapsed > GAMEOVER_TIMEOUT_MS) {
                _phase = PHASE_ATTRACT;
                _attractSlide      = SLIDE_GAME;
                _attractSlideTimer = millis();
                _attractMusicStarted = false;
                _btnBWasHeld = false;
            }
            return true;
        }

        // ---- PHASE: PLAYING ----
        updateDriving(input);
        updateKits(audio);
        tryFire(input, audio);
        updateEnemies(audio);
        updateShells(audio);

        _scene->render();
        drawSun(canvas);
        _particles.update(1.0f / 60.0f);
        _particles.render(_scene, &_camera, canvas.width(), canvas.height());

        drawBarrel(canvas);
        drawGunsight(canvas, canvas.width() / 2, 11 + (canvas.height() - 11) / 2);
        drawDamageFlash(canvas);
        drawRadar(canvas);
        drawHUD(canvas);

        if (_health <= 0) {
            if (_score > _highScore) { _highScore = _score; saveHighScore(); }
            _phase = PHASE_GAMEOVER;
            _gameOverEnteredMs = millis();
            audio.playTone(150, 400);
        }

        return true;
    }

    uint8_t getRotation() const override { return 1; }
    const char* getName()  const override { return "Tank Flux"; }
};

#endif // TANK_FLUX_GAME_H
