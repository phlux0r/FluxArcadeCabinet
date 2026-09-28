#ifndef TUBE_FLUX_GAME_H
#define TUBE_FLUX_GAME_H

#include "../../games/IGame.h"
#include "../../cabinet/ArcadeConfig.h"
#include <Jet.hpp>

#include "TubeFluxConfig.h"
#include "TubeMath.h"

// =============================================================================
// TUBE FLUX: fly down an endless octagonal tunnel, rolling round its wall to
// dodge blocks. Rendered with Jet, like Tank Flux.
//
// The ship is a 2D sprite fixed at the bottom of the screen; the camera
// rolls with it, so steering turns the tunnel rather than moving the ship.
// Speed only goes up: every TIER_DISTANCE a gate raises the tier, which
// raises the speed floor, packs the blocks closer and allows wider ones.
//
// The world is a treadmill: the camera stays at z = 0 and everything ahead
// is placed relative to the distance flown, so coordinates stay small no
// matter how long a run lasts.
//
// The tunnel itself isn't a Jet object. It covers every pixel, every frame,
// and Jet's general per-pixel path made it cost more than all of Tank
// Flux's worst case. It's flat-coloured octagon rings, so drawTunnel()
// fills them straight into the canvas using Jet's own projection, and Jet
// then draws the blocks and the ship sprite over it.
//
// Files: TubeFluxGame.cpp (phases, update loop), TubeFluxScene.cpp (scene,
// tunnel and block meshes, ship sprite), TubeFluxPlay.cpp (steering,
// obstacles, collisions, scoring), TubeFluxHud.cpp (overlays and menu
// screens).
// =============================================================================

namespace tubeflux {

class TubeFluxGame : public IGame {
public:
    TubeFluxGame() {}
    ~TubeFluxGame() override { releaseScene(); }

    void init(AudioEngine &audio) override;
    bool update(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio) override;
    void onExit() override { releaseScene(); }

    uint8_t getRotation() const override { return 1; }
    const char* getName()  const override { return "Tube Flux"; }

private:
    enum GamePhase { PHASE_ATTRACT, PHASE_PLAYING, PHASE_GAMEOVER };
    enum AttractSlide { SLIDE_TITLE, SLIDE_INFO, SLIDE_DEMO };
    enum PickupKind : uint8_t { PICKUP_GUN, PICKUP_UPGRADE, PICKUP_SHIELD };
    // See DRONE_* in TubeFluxConfig.h. ESCAPE is the drone flying off ahead
    // after surviving; the chase is over once it's gone.
    enum ChasePhase : uint8_t { CHASE_NONE, CHASE_PURSUE, CHASE_OVERTAKE, CHASE_AHEAD, CHASE_ESCAPE };
    // Bonus round (TubeFluxBonus.cpp): a portal waiting ahead, the round,
    // then the paused tally.
    enum BonusPhase : uint8_t { BONUS_NONE, BONUS_PORTAL, BONUS_ROUND, BONUS_TALLY };

    struct Obstacle {
        Renderer::Object* obj = nullptr;   // one mesh per width, see _blockMeshes
        bool  active = false;
        int   lanes = 1;                   // how many panels it covers
        int   lane = 0;                    // first panel, 0..TUBE_SIDES-1
        float at = 0.0f;                   // distance along the run where it sits
        bool  resolved = false;            // already hit or scored as it passed
        bool  crystal = false;             // destructible (lives in _crystals)
        int   points = 0;                  // bonus-round gems: what shooting it scores
    };

    // A drone bolt: comes from behind the camera down one lane.
    struct Bolt {
        Renderer::Object* obj = nullptr;
        bool  active = false;
        bool  resolved = false;            // already hit you or went past
        float z = 0.0f;                    // depth ahead of the camera
        float angle = 0.0f;
    };

    struct Shot {
        Renderer::Object* obj = nullptr;
        bool  active = false;
        float at = 0.0f;                   // distance along the run
        float angle = 0.0f;                // lane it was fired down
    };

    // --- Session ---------------------------------------------------------------
    GamePhase     _phase = PHASE_ATTRACT;
    bool          _btnBWasHeld = false;   // B must be released before hold-to-exit counts
    unsigned long _btnBHoldStart = 0;
    unsigned long _phaseEnteredMs = 0;
    AttractSlide  _attractSlide = SLIDE_TITLE;
    unsigned long _attractSlideAt = 0;
    // Attract demo (TubeFluxDemo.cpp): the autopilot plays a real run.
    unsigned long _demoUntil = 0;
    bool          _silent = false;          // gameplay sounds off (the demo is silent)
    bool          _pilotPrevA = false;      // so the autopilot's A makes presses
    int           _pilotTarget = 0;         // lane it's heading for
    unsigned long _pilotReplanAt = 0;
    float         _frameScale = 1.0f;
    unsigned long _lastFrameMs = 0;

    // --- Run -------------------------------------------------------------------
    float _angle = 0.0f;         // ship's position round the tunnel, degrees
    float _rollVel = 0.0f;       // degrees per frame
    float _throttle = 1.0f;      // multiplier on the tier's speed
    float _speed = BASE_SPEED;   // units per frame, this frame
    float _dist = 0.0f;          // distance flown this run
    float _nextSpawnAt = 0.0f;   // distance at which the next block appears
    int   _safeLane = 0;         // never blocked; see SAFE_LANE_SHIFT
    // Bends (see BEND_REF_Z): the current curve eases towards the target,
    // which is re-picked every BEND_SEGMENT of distance.
    float _bendX = 0.0f, _bendY = 0.0f;
    float _bendTargetX = 0.0f, _bendTargetY = 0.0f;
    float _nextBendAt = 0.0f;
    int   _prevSafeLane = 0;     // also kept open until the move's transition ends
    float _safeLaneMovedAt = 0.0f;
    int   _tier = 1;
    int   _shield = SHIELD_MAX;
    long  _score = 0;
    long  _bonus = 0;            // near misses and gates, added to distance score
    long  _highScore = 0;
    bool  _newHighScore = false;   // this run beat the old one
    unsigned long _invulnUntil = 0;
    unsigned long _hitFlashUntil = 0;
    unsigned long _nearMissUntil = 0;
    unsigned long _tierBannerUntil = 0;
    Obstacle _obstacles[OBSTACLE_POOL];
    Obstacle _crystals[CRYSTAL_POOL];
    Shot     _shots[SHOT_POOL];
    int   _gunLevel = 0;           // 0 unarmed, 1 gun, 2 twin, 3 rapid
    bool  armed() const { return _gunLevel > 0; }
    unsigned long _reloadAt = 0;
    int   _crystalsDestroyed = 0;
    int   _shieldsCollected = 0;
    // Pickups: one on the tunnel at a time, of any kind (updatePickup()
    // decides which is due). The gun and its upgrades share the chevron.
    Renderer::Object* _chevronObj = nullptr;
    Renderer::Object* _crossObj = nullptr;
    PickupKind _pickupKind = PICKUP_GUN;
    bool  _pickupActive = false;
    float _pickupAt = 0.0f;
    int   _pickupLane = 0;
    float _nextGunAt = WEAPON_FIRST_AT;
    float _nextUpgradeAt = 0.0f;
    float _nextShieldAt = 0.0f;
    // --- Drone chase (TubeFluxChase.cpp) ---
    ChasePhase    _chase = CHASE_NONE;
    int           _chaseCount = 0;          // chases started this run
    int           _chaseIndex = 0;          // this chase's number, from 0: sets difficulty
    float         _nextChaseAt = 0.0f;
    unsigned long _chasePhaseAt = 0;        // when the current phase began
    unsigned long _nextVolleyAt = 0;
    unsigned long _volleyFireAt = 0;        // warned lanes fire then; 0 = none pending
    uint8_t       _warnLanes = 0;           // bit per lane, flashing red on the walls
    Bolt          _bolts[DRONE_BOLT_POOL];
    Renderer::Object* _droneObj = nullptr;
    float         _droneAngle = 0.0f, _droneTargetAngle = 0.0f;
    float         _droneZ = 0.0f;
    unsigned long _droneRetargetAt = 0, _droneDropAt = 0, _droneFlashUntil = 0;
    int           _droneHp = 0, _droneMaxHp = 1;
    int           _dronesDestroyed = 0, _dronesEscaped = 0;
    unsigned long _chaseBannerUntil = 0;
    char          _chaseBanner[24] = "";
    uint16_t      _chaseBannerColour = 0xFFFF;

    // --- Bonus round (TubeFluxBonus.cpp) ---
    BonusPhase    _bonusPhase = BONUS_NONE;
    float         _nextPortalAt = PORTAL_FIRST_AT;
    float         _portalAt = 0.0f;         // centre of the portal's stretch of wall
    int           _portalLane = 0;
    unsigned long _bonusStartAt = 0;
    unsigned long _tallyAt = 0;
    float         _nextFormationAt = 0.0f;
    int           _gemsTotal = 0, _gemsHit = 0;
    long          _bonusRoundPoints = 0;
    bool          _bonusPerfect = false;
    Obstacle      _gems[BONUS_POOL];
    // Stats for the harness, per run.
    int           _portalsEntered = 0, _portalsMissed = 0, _bonusPerfects = 0;

    // One line of news under the HUD when a pickup is collected.
    unsigned long _pickupBannerUntil = 0;
    const char*   _pickupBanner = "";
    uint16_t      _pickupBannerColour = 0xFFFF;

    // --- Scene -----------------------------------------------------------------
    Renderer::Scene*  _scene = nullptr;
    Renderer::Camera  _camera;
    Renderer::ParticleSystem _particles{ (float)JET32_WORLD_SCALE };
    uint16_t _backdrop[ArcadeConfig::LANDSCAPE_HEIGHT];

    // Two-tone checkerboard walls: lanes and rings both read at a glance,
    // which is what tells you where you are and how fast you're going.
    uint16_t _wallA = 0, _wallB = 0;
    // Blocks: three tones of steel so the face towards you, the top and the
    // ends read as a solid at a glance.
    Renderer::Material _blockFrontMat{ 0xFFFF };
    Renderer::Material _blockTopMat{ 0xFFFF };
    Renderer::Material _blockSideMat{ 0xFFFF };
    // Crystals are hot orange, two tones so the facets read; shots cyan;
    // the pickup flashes yellow/white.
    Renderer::Material _crystalMatA{ 0xFFFF };
    Renderer::Material _crystalMatB{ 0xFFFF };
    Renderer::Material _shotMat{ 0xFFFF };
    Renderer::Material _pickupMat{ 0xFFFF };
    Renderer::Material _crossMat{ 0xFFFF };
    // The drone: dark red hull, a brighter top, a glowing engine. All flash
    // white when hit (TubeFluxChase.cpp).
    Renderer::Material _droneHullMat{ 0xFFFF };
    Renderer::Material _droneTopMat{ 0xFFFF };
    Renderer::Material _droneDarkMat{ 0xFFFF };
    Renderer::Material _droneEngineMat{ 0xFFFF };
    Renderer::Material _boltMat{ 0xFFFF };
    // Bonus-round gems, two tones each: green, cyan-white, gold.
    Renderer::Material _gemMat[3][2];
    // One mesh per obstacle slot; widths are fixed per slot (see
    // ensureSceneReady), which is what lets blocks be pooled.

    // Ship: two frames, left bank is the right bank flipped (Sprite2D::FLIP_X).
    // Jet's Texture takes a mutable pointer but only reads it; the art stays
    // in flash.
    Renderer::Texture*  _shipLevelTex = nullptr;
    Renderer::Texture*  _shipBankTex  = nullptr;
    Renderer::Material  _shipLevelMat{ 0xFFFF };
    Renderer::Material  _shipBankMat{ 0xFFFF };
    Renderer::Sprite2D  _shipSprite;

    // --- TubeFluxGame.cpp ------------------------------------------------------
    void loadHighScore();
    void saveHighScore();
    void recordHighScore();
    void updateFrameScale();
    void startNewGame(AudioEngine &audio);
    void resetRun();
    void stepRun(const InputState &input, AudioEngine &audio);
    void renderRun(GFXcanvas16 &canvas);
    // Gameplay sound goes through these, so the demo can be silent.
    void sfxWAV(AudioEngine &audio, const char* path) { if (!_silent) audio.playWAV(path); }
    void sfxTone(AudioEngine &audio, int hz, int ms)  { if (!_silent) audio.playTone(hz, ms); }
    void sfxMelody(AudioEngine &audio, const int* n, const int* d, int len) {
        if (!_silent) audio.playMelody(n, d, len);
    }
    void enterGameOver(AudioEngine &audio);
    void hideTransients();
    bool updateAttract(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    bool updateGameOver(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    bool updatePlaying(GFXcanvas16 &canvas, const InputState &input, AudioEngine &audio);
    void renderWorld(GFXcanvas16 &canvas);

    // --- TubeFluxScene.cpp -----------------------------------------------------
    void ensureSceneReady(GFXcanvas16 &canvas);
    void releaseScene();
    void buildBackdrop(int h);
    void applyTierPalette();
    void drawTunnel(GFXcanvas16 &canvas);
    Renderer::Object* buildBlock(int lanes);
    Renderer::Object* buildCrystal();
    Renderer::Object* buildGem(int type);
    Renderer::Object* buildCrystalMesh(Renderer::Material* a, Renderer::Material* b, float scale);
    Renderer::Object* buildPickup();
    Renderer::Object* buildCross();
    Renderer::Object* buildDrone();
    void placeCamera();
    void updateShipSprite();

    // --- TubeFluxPlay.cpp ------------------------------------------------------
    int   tierFor(float dist) const;
    float tierSpeed() const;
    float spawnGap() const;
    void  updateSteering(const InputState &input);
    void  updateSpeed(const InputState &input);
    void  updateTier(AudioEngine &audio);
    void  updateBend();
    void  bendOffset(float z, float &x, float &y) const;
    void  spawnObstacles();
    void  spawnBlock(float at);
    void  placeObstacle(Obstacle &o);
    void  updateObstacles(AudioEngine &audio);
    void  hitShip(Obstacle &o, AudioEngine &audio);
    void  damageShip(AudioEngine &audio);
    void  updateObstacle(Obstacle &o, AudioEngine &audio);
    bool  spawnCrystal(float at, int open, int openLanes);
    void  lanePoint(float angle, float radius, float z, float &x, float &y) const;
    void  updatePickup(AudioEngine &audio);
    bool  spawnDuePickup(float at);
    void  collectPickup(AudioEngine &audio);
    void  missPickup();
    Renderer::Object* pickupObj() const { return _pickupKind == PICKUP_SHIELD ? _crossObj : _chevronObj; }
    void  fireShot(float angle);
    void  tryFire(const InputState &input, AudioEngine &audio);
    void  updateShots(AudioEngine &audio);
    void  destroyCrystal(Obstacle &o, AudioEngine &audio);

    // --- TubeFluxChase.cpp -----------------------------------------------------
    bool  chasing() const { return _chase != CHASE_NONE; }
    void  startChase(AudioEngine &audio);
    void  updateChase(AudioEngine &audio);
    void  warnVolley(AudioEngine &audio);
    void  fireVolley(AudioEngine &audio);
    void  updateBolts(AudioEngine &audio);
    void  updateDroneAhead(AudioEngine &audio);
    void  dropCrystal(int lane, float at);
    void  placeDrone();
    bool  droneShotAt(float from, float to, float angle) const;
    void  hitDrone(AudioEngine &audio);
    void  endChase(bool destroyed);
    void  setChaseBanner(const char* text, uint16_t colour);
    void  hideChase();

    // --- TubeFluxBonus.cpp -----------------------------------------------------
    bool  inBonus() const { return _bonusPhase == BONUS_ROUND || _bonusPhase == BONUS_TALLY; }
    void  updatePortal(AudioEngine &audio);
    void  startBonusRound(AudioEngine &audio);
    void  updateBonusRound(AudioEngine &audio);
    void  spawnFormation(float at);
    bool  placeGem(int type, int lane, float at);
    void  updateGems();
    void  hitGem(Obstacle &g, AudioEngine &audio);
    void  startTally(AudioEngine &audio);
    void  endBonus();
    void  hideBonus();
    void  drawBonusHud(GFXcanvas16 &canvas);
    void  drawTally(GFXcanvas16 &canvas);

    // --- TubeFluxDemo.cpp ------------------------------------------------------
    bool inDemo() const { return _phase == PHASE_ATTRACT && _attractSlide == SLIDE_DEMO; }
    void startDemo();
    void updateDemo(GFXcanvas16 &canvas, AudioEngine &audio);
    void endDemo();
public:   // the autopilot is also the host harness's bot (test/tubeflux_harness.cpp)
    bool laneBlocked(int lane, float lookahead) const;
    InputState pilot(float lookahead, bool shootDrone, unsigned long replanMs);
private:

    // --- TubeFluxHud.cpp -------------------------------------------------------
    void drawHUD(GFXcanvas16 &canvas);
    void drawOverlays(GFXcanvas16 &canvas);
    void drawQuitHint(GFXcanvas16 &canvas);
    void drawCentred(GFXcanvas16 &canvas, const char* text, int y, uint16_t colour, uint8_t size = 1);
    void enterAttract();
    void renderAttractTitle(GFXcanvas16 &canvas);
    void renderAttractInfo(GFXcanvas16 &canvas);
    void renderGameOver(GFXcanvas16 &canvas);
};

}  // namespace tubeflux

using TubeFluxGame = tubeflux::TubeFluxGame;

#endif  // TUBE_FLUX_GAME_H
