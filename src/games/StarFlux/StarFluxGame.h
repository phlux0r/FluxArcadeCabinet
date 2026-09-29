#ifndef STAR_FLUX_GAME_H
#define STAR_FLUX_GAME_H

#include "../../games/IGame.h"
#include "../../cabinet/ArcadeConfig.h"
#include <Jet.hpp>

#include "StarFluxConfig.h"

// =============================================================================
// STAR FLUX: an on-rails space shooter, rendered with Jet like Tank and
// Tube Flux. The camera sits behind the ship; the ship moves round a box on
// screen while the stage flies at you: fighter waves in formation, rock
// fields, shield rings, then a boss.
//
// The ship is a 2D sprite (StarShipSprite.h, three bank frames), drawn
// where its 3D position projects. Fighters, rocks and the boss are Jet
// objects. The starfield, planet, lasers, enemy shots, rings, blasts and
// the reticle are drawn straight into the canvas with Jet's projection
// (project()), which is far cheaper than meshes for things that are only
// dots, lines and circles.
//
// A stage is a list of segments (StarFluxPlay.cpp): a wave, a rock field,
// or the boss. Losing your shield costs a life and restarts the segment
// you were in. Clearing the boss shows the results, then the stage loops,
// a little harder each time.
//
// Files: StarFluxGame.cpp (phases, update loop), StarFluxScene.cpp (scene,
// meshes, backdrop, 2D drawing), StarFluxPlay.cpp (ship, lasers, bombs,
// rocks, rings, the stage script), StarFluxEnemies.cpp (fighters, enemy
// shots, the boss), StarFluxHud.cpp (HUD and menu screens),
// StarFluxDemo.cpp (the autopilot and attract demo).
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
    enum GamePhase { PHASE_ATTRACT, PHASE_PLAYING, PHASE_RESULTS, PHASE_GAMEOVER };
    enum AttractSlide { SLIDE_TITLE, SLIDE_INFO, SLIDE_DEMO };
    enum SegType : uint8_t { SEG_WAVE, SEG_ROCKS, SEG_BOSS };
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
        uint16_t lengthMs;    // rock fields
        bool     ring;        // a shield ring comes with it
    };

    struct Fighter {
        Renderer::Object* obj = nullptr;
        bool  active = false;
        Pattern pattern = PAT_VDIVE;
        int8_t  mirror = 1;
        uint8_t nextFire = 0;         // index into the pattern's fire times
        uint8_t wave = 0;             // the segment that launched it
        unsigned long startAt = 0;    // when it starts its path (staggered per slot)
        float ox = 0, oy = 0;         // formation offset
        float x = 0, y = 0, z = 0;
    };
    struct Shot  { bool active = false; float x = 0, y = 0, z = 0, pz = 0; };   // pz: last frame's z
    struct EShot { bool active = false; float x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0; };   // v per frame
    struct Rock {
        Renderer::Object* obj = nullptr;
        bool  active = false;
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
    long          _highScore = 0;
    bool          _newHighScore = false;

    // --- Run -------------------------------------------------------------------
    int   _loop = 1;                 // times round the stage, from 1: difficulty
    int   _lives = LIVES;
    int   _shield = SHIELD_MAX;
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
    bool    _waveClean[12] = {};
    unsigned long _pilotReplanAt = 0;
    unsigned long _nextRockAt = 0;
    bool  _segRingDone = false;
    bool  _retrying = false;         // restarting a segment after losing a life
    unsigned long _invulnUntil = 0;
    unsigned long _hitFlashUntil = 0;
    unsigned long _lastShotAt = 0;
    bool  _prevA = false;
    unsigned long _btnBDownAt = 0;   // for telling a bomb tap from a quit hold
    // Stats for the results screen, per stage.
    int   _fightersSeen = 0, _fightersDowned = 0, _rocksDowned = 0, _ringsCaught = 0;
    long  _stageStartScore = 0;
    long  _shieldBonus = 0;
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
    Blast    _blasts[8];
    Star     _stars[STAR_COUNT];
    bool     _bombActive = false;
    float    _bombX = 0, _bombY = 0, _bombZ = 0;

    // --- Boss (StarFluxEnemies.cpp) ---
    bool  _bossActive = false;
    unsigned long _bossAt = 0;             // when it appeared
    float _bossX = 0, _bossY = 0, _bossZ = BOSS_ENTER_Z;
    int   _cannonHp[2] = { 0, 0 };
    int   _coreHp = 0;
    int   _bossMaxHp = 1;
    unsigned long _cannonFireAt = 0, _burstAt = 0, _coreFireAt = 0;
    int   _cannonTurn = 0;
    unsigned long _cannonFlash[2] = { 0, 0 }, _coreFlash = 0;
    unsigned long _nextBossBlastAt = 0;
    int   _bossAlarms = 0;               // warning beeps sounded so far
    bool  coreOpen() const { return _cannonHp[0] <= 0 && _cannonHp[1] <= 0; }
    int   bossHp() const { return (_cannonHp[0] > 0 ? _cannonHp[0] : 0) + (_cannonHp[1] > 0 ? _cannonHp[1] : 0) + (_coreHp > 0 ? _coreHp : 0); }

    // --- Scene -----------------------------------------------------------------
    Renderer::Scene*  _scene = nullptr;
    Renderer::Camera  _camera;
    Renderer::ParticleSystem _particles{ (float)JET32_WORLD_SCALE };
    Renderer::DirectionalLight _sun{ Vector3{ -40, 45, 0 }, Renderer::Color{ 255, 236, 210 }, 255 };
    Renderer::AmbientLight     _amb{ Renderer::Color{ 70, 64, 90 } };
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
    Renderer::Object*  _bossHull = nullptr;
    Renderer::Object*  _cannonObj[2] = { nullptr, nullptr };
    Renderer::Object*  _coreObj = nullptr;
    Renderer::Object*  _shieldObj = nullptr;

    // Ship: level, bank and hard bank; left is right flipped (Sprite2D::FLIP_X).
    // Jet's Texture takes a mutable pointer but only reads it; the art stays in flash.
    Renderer::Texture*  _shipTex[3] = { nullptr, nullptr, nullptr };
    Renderer::Material  _shipMat[3];
    Renderer::Sprite2D  _shipSprite;

    // --- StarFluxGame.cpp ------------------------------------------------------
    void loadHighScore();
    void saveHighScore();
    void recordHighScore();
    void updateFrameScale();
    void startNewGame(AudioEngine &audio);
    void resetRun();
    void startStage();
    void stepRun(const InputState &input, AudioEngine &audio);
    void renderRun(GFXcanvas16 &canvas);
    void sfxWAV(AudioEngine &audio, const char* path) { if (!_silent) audio.playWAV(path); }
    void sfxTone(AudioEngine &audio, int hz, int ms)  { if (!_silent) audio.playTone(hz, ms); }
    void enterGameOver(AudioEngine &audio);
    void enterResults(AudioEngine &audio);
    void clearField();
    bool updateAttract(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    bool updateResults(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    bool updateGameOver(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
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
    void placeCamera();
    bool project(float x, float y, float z, float &sx, float &sy) const;
    float pixelsPerUnit(float z) const { return _camera.fovFactor / z; }
    void drawBackdrop(GFXcanvas16 &canvas);
    void drawStars(GFXcanvas16 &canvas);
    void drawRings(GFXcanvas16 &canvas);
    void drawShots(GFXcanvas16 &canvas);
    void drawBlasts(GFXcanvas16 &canvas);
    void drawReticle(GFXcanvas16 &canvas);
    void updateShipSprite();
    void setFlash(Renderer::Object* o, bool flash, Renderer::Material* normalA, Renderer::Material* normalB);

    // --- StarFluxPlay.cpp ------------------------------------------------------
    const Segment& segment() const;
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
    void  spawnRock(bool aimed);
    void  updateRocks(AudioEngine &audio);
    void  destroyRock(Rock &r, bool byPlayer, AudioEngine &audio);
    void  spawnRing();
    void  updateRings(AudioEngine &audio);
    void  damageShip(int amount, AudioEngine &audio);
    void  shipDown(AudioEngine &audio);
    void  retrySegment();
    void  addBlast(float x, float y, float z, float size, uint16_t colour);
    void  setBanner(const char* text, uint16_t colour, unsigned long ms);
    bool  shipControllable() const { return _stage == STAGE_RUN; }

    // --- StarFluxEnemies.cpp ---------------------------------------------------
    void  spawnWaveFighters();
    void  pathPoint(const Fighter &f, unsigned long ms, float &x, float &y, float &z) const;
    unsigned long patternLength(Pattern p) const;
    unsigned long patternStagger(Pattern p) const;
    void  updateFighters(AudioEngine &audio);
    void  destroyFighter(Fighter &f, bool byPlayer, AudioEngine &audio);
    void  fighterGone(Fighter &f, bool downed, AudioEngine &audio);
    void  fireAt(float x, float y, float z, float tx, float ty, float speedMul = 1.0f);
    void  updateEShots(AudioEngine &audio);
    void  startBoss();
    void  updateBoss(AudioEngine &audio);
    void  placeBoss();
    void  ringBurst(float x, float y, float z);
    void  hitBossPart(int part, int damage, AudioEngine &audio);   // 0,1 cannons, 2 core
    void  bossPartPos(int part, float &x, float &y, float &z) const;
    bool  bossPartAlive(int part) const;
    void  hideBoss();
    int   firePct() const { return FIRE_PCT + FIRE_PCT_PER_LOOP * (_loop - 1); }
    float eshotSpeed() const { return ESHOT_SPEED + ESHOT_SPEED_PER_LOOP * (float)(_loop - 1); }

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
    void renderResults(GFXcanvas16 &canvas);
    void renderGameOver(GFXcanvas16 &canvas);
};

}  // namespace starflux

using StarFluxGame = starflux::StarFluxGame;

#endif  // STAR_FLUX_GAME_H
