#ifndef STAR_FLUX_GAME_H
#define STAR_FLUX_GAME_H

#include "../../games/IGame.h"
#include "../../cabinet/ArcadeConfig.h"
#include <Jet.hpp>

#include "StarFluxConfig.h"
#include "../../cabinet/HighScores.h"

// =============================================================================
// STAR FLUX: an on-rails space shooter, rendered with Jet like Tank and
// Tube Flux. The camera sits behind the ship; the ship moves round a box on
// screen while the stage flies at you: fighter waves in formation, hazard
// fields, shield rings, then a boss. Five stages: an asteroid belt in
// space, a planet's surface, a trench run on a space station, an ice
// canyon, and the mothership's hull.
//
// The ship is a 2D sprite (StarShipSprite.h, three bank frames), drawn
// where its 3D position projects. Fighters, rocks, obstacles, turrets and
// the bosses are Jet objects. The backdrops (space, the planet's ground
// and sky, the trench, the canyon, the hull), mines, the lasers, enemy shots, rings, blasts and the
// reticle are drawn straight into the canvas with Jet's projection
// (project()): far cheaper than meshes for whole-screen fills and for
// things that are only dots, lines and circles.
//
// A stage is a list of segments (StarFluxPlay.cpp): a wave, a hazard field
// (rocks; pillars and turret towers; trench barriers, laser gates and
// turrets; icicles, ice arches and pillars, and mines; gun towers, blast
// doors, force fields and masts), or the boss.
// Losing your shield costs a life and restarts the segment you were in.
// Each boss ends its stage with a results screen; after the fifth, the
// game loops back to stage 1, harder (StarFluxConfig.h's Loops block).
//
// Files: StarFluxGame.cpp (phases, update loop), StarFluxScene.cpp (scene,
// meshes, space backdrop, 2D drawing), StarFluxWorld.cpp (the planet and
// trench backdrops, obstacles, gates, turrets), StarFluxPlay.cpp (ship,
// lasers, bombs, rocks, rings, the stage scripts), StarFluxEnemies.cpp
// (fighters, enemy shots and missiles), StarFluxBoss.cpp (the three
// bosses), StarFluxHud.cpp (HUD and menu screens), StarFluxDemo.cpp (the
// autopilot and attract demo).
// =============================================================================

namespace starflux {

// Wrap-safe millis() deadlines.
inline bool reached(unsigned long deadline) { return (long)(millis() - deadline) >= 0; }
inline bool before(unsigned long deadline)  { return (long)(millis() - deadline) < 0; }

class StarFluxGame : public IGame {
public:
    StarFluxGame() {}
    ~StarFluxGame() override { releaseScene(); }

    void init(AudioEngine &audio) override;
    bool update(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) override;
    void onExit() override { releaseScene(); }

    uint8_t getRotation() const override { return 1; }
    const char* getName()  const override { return "Star Flux"; }

private:
    // NAME: entering a name for the high-score table, after the last life.
    enum GamePhase { PHASE_ATTRACT, PHASE_PLAYING, PHASE_RESULTS, PHASE_NAME, PHASE_GAMEOVER };
    enum AttractSlide { SLIDE_TITLE, SLIDE_INFO, SLIDE_SCORES, SLIDE_DEMO };
    enum SegType : uint8_t { SEG_WAVE, SEG_FIELD, SEG_BOSS };
    enum StageId : uint8_t { STAGE_BELT, STAGE_PLANET, STAGE_TRENCH, STAGE_CANYON, STAGE_MOTHER, STAGE_COUNT };
    enum Pattern : uint8_t { PAT_VDIVE, PAT_SWEEP, PAT_HEADON, PAT_LOOP, PAT_WEAVE, PAT_COUNT };
    // INTRO: the fly-in with the stage name. DOWN: you've been shot down
    // and the world flies on without you for a moment. BOSS_DEATH: the
    // boss's last explosions, before the results.
    enum StageState : uint8_t { STAGE_INTRO, STAGE_RUN, STAGE_DOWN, STAGE_BOSS_DEATH };

    struct Segment {
        SegType  type;
        Pattern  pattern;     // waves
        uint8_t  count;       // waves: fighters
        int8_t   mirror;      // waves: +1 as drawn, -1 mirrored, 0 alternate sides
        uint16_t lengthMs;    // hazard fields
        bool     ring;        // a shield ring comes with it
    };

    // Optional sounds (see the list in README.md): each is played if it's on
    // the card, else its fallback (another WAV, or a tone).
    enum Sfx : uint8_t { SFX_POP, SFX_HIT, SFX_ARMOR, SFX_BOSS_WARN, SFX_BOSS_FIRE, SFX_BURST,
                         SFX_PART_DOWN, SFX_CORE_OPEN, SFX_BOSS_DIE, SFX_BOMB, SFX_RING, SFX_POWER, SFX_EXTRA, SFX_COUNT };

    struct Fighter {
        Renderer::Object* obj = nullptr;
        bool  active = false;
        Pattern pattern = PAT_VDIVE;
        int8_t  mirror = 1;
        uint8_t nextFire = 0;         // index into the pattern's fire times
        uint8_t wave = 0;             // the segment that launched it
        unsigned long startAt = 0;    // when it starts its path (staggered per slot)
        float ox = 0, oy = 0;         // formation offset
        float tight = 1;              // a V's offsets are scaled to this mid-dive (whole formation alike)
        float x = 0, y = 0, z = 0;
    };
    struct Shot  { bool active = false; float x = 0, y = 0, z = 0, pz = 0; };   // pz: last frame's z
    // Enemy fire, v per frame. A homing one is a missile: it steers at you
    // for a while and can be shot down.
    // A frost one (the walker's) also slows your steering when it hits.
    struct EShot { bool active = false, homing = false, frost = false; float x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0; };
    // An obstacle: an axis-aligned block (a pillar, a turret tower, a trench
    // barrier), or a laser gate, which is drawn in 2D and blinks.
    struct Box {
        Renderer::Object* obj = nullptr;   // null for gates
        bool  active = false, hit = false, gate = false;
        float x0 = 0, x1 = 0, y0 = 0, y1 = 0, z = 0, depth = 0;
        unsigned long phase = 0;           // gates: offset into the blink; doors: into the cycle
        // A blast door slides: x0, x1 are rx0, rx1 moved by slide times how
        // closed it is (0 open .. 1 shut, doorShut()). slide 0: it doesn't.
        float slide = 0, rx0 = 0, rx1 = 0;
    };
    // A gun emplacement on a tower: shoots at you, takes TURRET_HP hits.
    struct Turret {
        Renderer::Object* obj = nullptr;
        bool  active = false;
        int   hp = TURRET_HP;
        float x = 0, y = 0, z = 0;
        unsigned long fireAt = 0, flashUntil = 0;
    };
    // A mine (the canyon's) is a small rock's slot drawn in 2D, steering at you.
    struct Rock {
        Renderer::Object* obj = nullptr;
        bool  active = false;
        bool  mine = false;
        int   hp = 1;
        float r = ROCK_SMALL_R;
        float x = 0, y = 0, z = 0, vx = 0, vy = 0;
        float ax = 0, ay = 0, az = 0;       // tumble, degrees
        float sx = 0, sy = 0, sz = 0;       // tumble rate, degrees per frame
        unsigned long flashUntil = 0;
    };
    struct Ring  { bool active = false, resolved = false; float x = 0, y = 0, z = 0; };
    // An explosion's flash: an expanding, fading disc, drawn in 2D.
    struct Blast { bool active = false; float x = 0, y = 0, z = 0, size = 0; unsigned long at = 0; uint16_t colour = 0; };
    struct Star  { float x = 0, y = 0, z = 0; };

    // --- Session ---------------------------------------------------------------
    GamePhase     _phase = PHASE_ATTRACT;
    bool          _btnBWasHeld = false;   // B must be released before it counts
    unsigned long _btnBHoldStart = 0;
    unsigned long _phaseEnteredMs = 0;
    AttractSlide  _attractSlide = SLIDE_TITLE;
    unsigned long _attractSlideAt = 0;
    unsigned long _demoUntil = 0;
    bool          _silent = false;        // gameplay sounds off (the demo is silent)
    float         _frameScale = 1.0f;
    unsigned long _lastFrameMs = 0;
    hiscore::ScoreBoard _scores;          // the cabinet's table for this game

    // --- Run -------------------------------------------------------------------
    int   _loop = 1;                 // times round all the stages, from 1: difficulty
    int   _stageNum = STAGE_BELT;    // which stage
    int   _lives = LIVES;
    int   _shield = SHIELD_MAX;          // up to SHIELD_CAP: past SHIELD_MAX is overcharge
    long  _nextLifeAt = EXTRA_LIFE_FIRST;
    int   _bombs = BOMBS_START;
    long  _score = 0;
    StageState _stage = STAGE_INTRO;
    unsigned long _stageAt = 0;      // when _stage was entered
    int   _seg = 0;                  // current segment of the stage script
    unsigned long _segAt = 0;        // when it started
    int   _segSpawned = 0;           // waves: fighters launched so far
    // Per wave (by segment): fighters still out, and whether any got away.
    // Waves overlap, so these can't just be the current segment's.
    uint8_t _waveLeft[12] = {};
    uint8_t _waveToCome[12] = {};    // not launched yet: waiting for a free slot
    bool    _waveClean[12] = {};
    unsigned long _segLaunchedAt = 0;   // waves: when a fighter was last launched
    unsigned long _pilotReplanAt = 0;
    unsigned long _nextFieldAt = 0;  // next rock, obstacle or turret in a field
    int   _fieldCount = 0;           // spawned so far this field
    bool  _segRingDone = false;
    bool  _retrying = false;         // restarting a segment after losing a life
    unsigned long _invulnUntil = 0;
    unsigned long _hitFlashUntil = 0;
    unsigned long _frozenUntil = 0;     // frost shard hit: steering slowed
    unsigned long _lastShotAt = 0;
    bool  _prevA = false;
    unsigned long _btnBDownAt = 0;   // for telling a bomb tap from a quit hold
    // Stats for the results screen, per stage.
    int   _fightersSeen = 0, _fightersDowned = 0, _targetsDowned = 0, _ringsCaught = 0;
    long  _stageStartScore = 0;
    long  _shieldBonus = 0;
    int   _wavesPerfect = 0;             // this stage's waves shot down whole
    long  _allPerfectBonus = 0;
    int   wavesInStage() const;
    // Banners: one line under the HUD.
    unsigned long _bannerUntil = 0;
    const char*   _banner = "";
    uint16_t      _bannerColour = 0xFFFF;

    // Ship: position in its box (world units at SHIP_Z), velocity, bank -1..1.
    float _shipX = 0, _shipY = 0, _shipVX = 0, _shipVY = 0, _bank = 0;
    float _camX = 0, _camY = 0, _camRoll = 0;   // as set on the Jet camera this frame
    float _rollCos = 1, _rollSin = 0;          // of minus the camera's roll: project() uses them

    Shot     _shots[SHOT_POOL];
    EShot    _eshots[ESHOT_POOL];
    Fighter  _fighters[FIGHTER_POOL];
    Rock     _rocks[ROCK_POOL];
    Ring     _rings[RING_POOL];
    Ring     _pod;                   // the rapid-fire pod (a ring's worth of state)
    bool     _rapid = false;         // got it: double fire rate until a life is lost
    bool     _segPodDone = false;
    Blast    _blasts[8];
    Box      _boxes[BOX_POOL];
    Turret   _turrets[TURRET_POOL];
    float    _groundScroll = 0;      // planet and trench: distance flown, for the floor pattern
    uint8_t  _mountains[64];         // planet: the ridge's height round the horizon
    bool     _sfxOnCard[SFX_COUNT] = {};
    Star     _stars[STAR_COUNT];
    bool     _bombActive = false;
    float    _bombX = 0, _bombY = 0, _bombZ = 0;

    // --- Boss (StarFluxBoss.cpp) ---
    // Every boss has two outer weak points (parts 0 and 1: cannons, missile
    // pods, emitters) and a core (part 2) that opens once both are gone.
    bool  _bossActive = false;
    int   _bossKind = STAGE_BELT;          // which stage's boss
    unsigned long _bossAt = 0;             // when it appeared
    float _bossX = 0, _bossY = 0, _bossZ = BOSS_ENTER_Z;
    float _bossVX = 0;                     // sideways, per frame: for leading it
    int   _cannonHp[2] = { 0, 0 };
    int   _coreHp = 0;
    int   _bossMaxHp = 1;
    unsigned long _cannonFireAt = 0, _burstAt = 0, _coreFireAt = 0;
    int   _cannonTurn = 0;
    unsigned long _cannonFlash[2] = { 0, 0 }, _coreFlash = 0;
    unsigned long _nextBossBlastAt = 0;
    int   _bossAlarms = 0;               // warning beeps sounded so far
    float _fanAngle = 0;                   // the reactor's shield fan, degrees
    unsigned long _spiralShotAt = 0;       // the reactor's spiral: next shot
    bool  coreOpen() const { return _cannonHp[0] <= 0 && _cannonHp[1] <= 0; }
    int   bossHp() const { return (_cannonHp[0] > 0 ? _cannonHp[0] : 0) + (_cannonHp[1] > 0 ? _cannonHp[1] : 0) + (_coreHp > 0 ? _coreHp : 0); }

    // --- Scene -----------------------------------------------------------------
    Renderer::Scene*  _scene = nullptr;
    Renderer::Camera  _camera;
    Renderer::ParticleSystem _particles{ (float)JET32_WORLD_SCALE };
    Renderer::DirectionalLight _sun{ Vector3{ -35, 25, 0 }, Renderer::Color{ 255, 236, 210 }, 255 };
    Renderer::AmbientLight     _amb{ Renderer::Color{ 120, 112, 132 } };
    uint16_t _sky[ArcadeConfig::LANDSCAPE_HEIGHT];
    uint16_t _planet[PLANET_D * PLANET_D];   // 0 = transparent

    Renderer::Material _fHullMat{ 0xFFFF }, _fDarkMat{ 0xFFFF }, _fBladeMat{ 0xFFFF };
    Renderer::Material _fTipMat{ 0xFFFF }, _fEngineMat{ 0xFFFF };
    Renderer::Material _rockMat[2];
    Renderer::Material _flashMat{ 0xFFFF };
    Renderer::Material _bossTopMat{ 0xFFFF }, _bossSideMat{ 0xFFFF }, _bossDarkMat{ 0xFFFF };
    Renderer::Material _bossFinMat{ 0xFFFF }, _bossLightMat{ 0xFFFF };
    Renderer::Material _cannonMat{ 0xFFFF }, _cannonMat2{ 0xFFFF };
    Renderer::Material _coreMat{ 0xFFFF }, _coreMat2{ 0xFFFF }, _shieldMat{ 0xFFFF };
    Renderer::Material _boxMat{ 0xFFFF }, _boxLightMat{ 0xFFFF };
    Renderer::Material _turretMat{ 0xFFFF }, _turretMat2{ 0xFFFF };
    Renderer::Material _crawlerMat{ 0xFFFF }, _crawlerDarkMat{ 0xFFFF };
    Renderer::Material _reactorMat{ 0xFFFF }, _reactorDarkMat{ 0xFFFF };
    Renderer::Material _walkerMat{ 0xFFFF }, _walkerDarkMat{ 0xFFFF };
    Renderer::Material _motherMat{ 0xFFFF }, _motherDarkMat{ 0xFFFF };
    uint8_t _skyline[64] = {};          // the mothership's towers on the horizon
    int     _volley = 0;                // the mothership core's attacks take turns
    Renderer::Object*  _bossHulls[STAGE_COUNT] = {};
    Renderer::Object*  _bossHull = nullptr;   // this stage's, one of _bossHulls
    Renderer::Object*  _cannonObj[2] = { nullptr, nullptr };
    Renderer::Object*  _coreObj = nullptr;
    Renderer::Object*  _shieldObj = nullptr;

    // Ship: level, bank and hard bank; left is right flipped (Sprite2D::FLIP_X).
    // Jet's Texture takes a mutable pointer but only reads it; the art stays in flash.
    Renderer::Texture*  _shipTex[3] = { nullptr, nullptr, nullptr };
    Renderer::Material  _shipMat[3];
    Renderer::Sprite2D  _shipSprite;

    // --- StarFluxGame.cpp ------------------------------------------------------
    void recordQuit();
    void updateFrameScale();
    void startNewGame(AudioEngine &audio);
    void resetRun();
    void startStage();
    void stepRun(const InputState &input, AudioEngine &audio);
    void renderRun(GFXcanvas16 &canvas);
    void sfxWAV(AudioEngine &audio, const char* path) { if (!_silent) audio.playWAV(path); }
    void sfx(AudioEngine &audio, Sfx s);
    void findSounds(AudioEngine &audio);
    void sfxTone(AudioEngine &audio, int hz, int ms)  { if (!_silent) audio.playTone(hz, ms); }
    void enterGameOver(AudioEngine &audio);
    void enterResults(AudioEngine &audio);
    void clearField();
    bool updateAttract(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    bool updateResults(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    bool updateGameOver(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    bool updateName(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    bool updatePlaying(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    void renderWorld(GFXcanvas16 &canvas);

    // --- StarFluxScene.cpp -----------------------------------------------------
    void ensureSceneReady(GFXcanvas16 &canvas);
    void releaseScene();
    void buildBackdrop(int h);
    void buildPlanet();
    Renderer::Object* buildFighter();
    Renderer::Object* buildRock(int seed, float radius, Renderer::Material* mat);
    Renderer::Object* buildBossHull();
    Renderer::Object* buildGem(float r, Renderer::Material* a, Renderer::Material* b);
    Renderer::Object* buildShieldPlate();
    Renderer::Object* buildCrawler();
    Renderer::Object* buildReactor();
    Renderer::Object* buildWalker();
    Renderer::Object* buildMothership();
    Renderer::Object* buildTurret();
    Renderer::Object* buildObstacleBox();
    void placeCamera();
    bool project(float x, float y, float z, float &sx, float &sy) const;
    float pixelsPerUnit(float z) const { return _camera.fovFactor / z; }
    void drawBackdrop(GFXcanvas16 &canvas);
    void drawSpace(GFXcanvas16 &canvas);
    void drawStars(GFXcanvas16 &canvas);
    void drawRings(GFXcanvas16 &canvas);
    void drawPod(GFXcanvas16 &canvas);
    void drawMines(GFXcanvas16 &canvas);
    void drawCanyon(GFXcanvas16 &canvas);
    void drawHull(GFXcanvas16 &canvas);
    void drawFlightAids(GFXcanvas16 &canvas);
    void drawShots(GFXcanvas16 &canvas);
    void drawBlasts(GFXcanvas16 &canvas);
    void drawReticle(GFXcanvas16 &canvas);
    void updateShipSprite();
    void setFlash(Renderer::Object* o, bool flash, Renderer::Material* normalA, Renderer::Material* normalB);

    // --- StarFluxPlay.cpp ------------------------------------------------------
    const Segment& segmentAt(int stage, int seg) const;
    const Segment& segment() const { return segmentAt(_stageNum, _seg); }
    int   segmentCount() const;
    void  startSegment(int index);
    void  updateStage(AudioEngine &audio);
    bool  segmentDone() const;
    void  updateShip(const InputState &input);
    void  tryFire(const InputState &input, AudioEngine &audio);
    void  updateBombButton(const InputState &input, AudioEngine &audio);
    void  dropBomb(AudioEngine &audio);
    void  updateBomb(AudioEngine &audio);
    void  detonateBomb(AudioEngine &audio);
    void  updateShots(AudioEngine &audio);
    bool  shotHits(const Shot &s, float x, float y, float z, float r) const;
    void  spawnField(AudioEngine &audio);
    void  spawnRock(bool aimed);
    void  updateRocks(AudioEngine &audio);
    void  destroyRock(Rock &r, bool byPlayer, AudioEngine &audio);
    bool  spawnRing();
    void  updateRings(AudioEngine &audio);
    bool  spawnPod();
    void  spawnMine();
    void  spawnCanyonHazard();
    void  spawnMotherHazard();
    void  spawnDoors(float gx, float z);
    float doorShut(const Box &b, unsigned long at) const;
    void  boxXAt(const Box &b, unsigned long at, float &x0, float &x1) const;
    bool  hasFan() const { return _bossKind == STAGE_TRENCH || _bossKind == STAGE_MOTHER; }    float canyonHalfW(float y) const { return CANYON_HALF_W + CANYON_SLOPE * (y - CANYON_FLOOR); }
    void  updatePod(AudioEngine &audio);
    bool  nextObstacle(float &front) const;
    bool  passClear(float front) const;
    void  damageShip(int amount, AudioEngine &audio);
    void  shipDown(AudioEngine &audio);
    void  retrySegment();
    void  addBlast(float x, float y, float z, float size, uint16_t colour);
    void  setBanner(const char* text, uint16_t colour, unsigned long ms);
    bool  shipControllable() const { return _stage == STAGE_RUN; }

    // --- StarFluxWorld.cpp ------------------------------------------------------
    void  buildMountains();
    void  drawPlanet(GFXcanvas16 &canvas);
    void  drawTrench(GFXcanvas16 &canvas);
    void  drawShadow(GFXcanvas16 &canvas);
    void  drawGates(GFXcanvas16 &canvas);
    void  drawFan(GFXcanvas16 &canvas);
    float floorY() const;              // ground or trench floor; far below in space
    bool  starVisible(float x, float y) const;
    void  applyStagePalette();
    Box*  spawnBox(float x0, float x1, float y0, float y1, float z, float depth);
    Box*  spawnGate(float y0, float y1, float z);
    void  spawnTower(float x, float z, bool withTurret);
    void  spawnPlanetHazard();
    void  spawnTrenchHazard();
    bool  gateOn(const Box &b, unsigned long at) const;
    void  updateBoxes(AudioEngine &audio);
    bool  shotBlocked(const Shot &s) const;
    void  updateTurrets(AudioEngine &audio);
    void  destroyTurret(Turret &t, bool byPlayer, AudioEngine &audio);
    void  hideWorld();
    const char* stageName() const;

    // --- StarFluxEnemies.cpp ---------------------------------------------------
    void  spawnWaveFighters();
    void  pathPoint(const Fighter &f, unsigned long ms, float &x, float &y, float &z) const;
    unsigned long patternLength(Pattern p) const;
    unsigned long patternStagger(Pattern p) const;
    void  updateFighters(AudioEngine &audio);
    void  destroyFighter(Fighter &f, bool byPlayer, AudioEngine &audio);
    void  fighterGone(Fighter &f, bool downed, AudioEngine &audio);
    EShot* fireAt(float x, float y, float z, float tx, float ty, float speedMul = 1.0f);
    void  fireMissile(float x, float y, float z);
    void  updateEShots(AudioEngine &audio);

    // --- StarFluxBoss.cpp ------------------------------------------------------
    void  startBoss();
    void  updateBoss(AudioEngine &audio);
    void  placeBoss();
    void  ringBurst(float x, float y, float z, bool frost = false);
    void  bossAttacks(AudioEngine &audio);
    bool  fanBlocks(float x, float y) const;
    float bossZ() const;
    void  bossHullSphere(float &x, float &y, float &z, float &r) const;
    const char* bossName() const;
    void  hitBossPart(int part, int damage, AudioEngine &audio);   // 0,1 cannons, 2 core
    void  bossPartPos(int part, float &x, float &y, float &z) const;
    bool  bossPartAlive(int part) const;
    void  hideBoss();
    // Difficulty by loop (see StarFluxConfig.h's Loops block).
    int   steps() const { return (_loop < LOOP_CAP ? _loop : LOOP_CAP) - 1; }
    int   firePct() const { return FIRE_PCT + FIRE_PCT_PER_LOOP * steps(); }
    float eshotSpeed() const { return ESHOT_SPEED + ESHOT_SPEED_PER_LOOP * (float)steps(); }
    int   leadPct() const { return LEAD_PCT + LEAD_PCT_PER_LOOP * steps(); }
    int   burstPct() const { return _loop < BURST_FROM_LOOP ? 0 : BURST_PCT + BURST_PCT_PER_LOOP * (steps() + 1 - BURST_FROM_LOOP); }
    int   waveCount(int base) const { int n = base + WAVE_EXTRA_PER_LOOP * steps(); return n > WAVE_MAX ? WAVE_MAX : n; }
    // A fighter's time along its path: quicker each loop.
    unsigned long flightMs(const Fighter &f) const {
        return (millis() - f.startAt) * (unsigned long)(100 + FIGHTER_PACE_PER_LOOP * steps()) / 100;
    }
    unsigned long bossMs(unsigned long ms) const { return ms * 100 / (unsigned long)(100 + BOSS_PACE_PER_LOOP * steps()); }
    unsigned long fieldMs(unsigned long ms) const { return ms * (unsigned long)(100 - FIELD_DENSER_PER_LOOP * steps()) / 100; }
    bool  pickupClear(float x, float y, float z) const;
    bool  pickupNear(float z) const;
    bool  placePickup(float &x, float &y, float z) const;
    void  checkExtraLife(AudioEngine &audio);

    // --- StarFluxDemo.cpp ------------------------------------------------------
    bool inDemo() const { return _phase == PHASE_ATTRACT && _attractSlide == SLIDE_DEMO; }
    void startDemo();
    void updateDemo(GFXcanvas16 &canvas, AudioEngine &audio);
    void endDemo();
    float threatAt(float x, float y) const;
    bool  targetOnLine(float x, float y) const;
    bool  pickAim(float &x, float &y) const;
    float planPilot();
    float _pilotX = 0, _pilotY = 0;
    bool  _pilotPrevB = false;
    unsigned long _pilotBombAt = 0;
public:   // the autopilot is also the host harness's bot (test/starflux_harness.cpp)
    InputState pilot(bool useBombs, unsigned long replanMs);
private:

    // --- StarFluxHud.cpp -------------------------------------------------------
    void drawHUD(GFXcanvas16 &canvas);
    void drawOverlays(GFXcanvas16 &canvas);
    void drawQuitHint(GFXcanvas16 &canvas);
    void drawCentred(GFXcanvas16 &canvas, const char* text, int y, uint16_t colour, uint8_t size = 1);
    void enterAttract();
    void renderAttractTitle(GFXcanvas16 &canvas);
    void renderAttractInfo(GFXcanvas16 &canvas);
    void renderAttractScores(GFXcanvas16 &canvas);
    void renderResults(GFXcanvas16 &canvas);
    void renderGameOver(GFXcanvas16 &canvas);
};

}  // namespace starflux

using StarFluxGame = starflux::StarFluxGame;

#endif  // STAR_FLUX_GAME_H
