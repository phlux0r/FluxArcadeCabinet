// JetConfig.hpp — Flux Arcade Cabinet's Jet configuration.
//
// platformio.ini forces this project's include/ directory onto Jet's own
// include path with an explicit -Iinclude build flag — PlatformIO's library
// dependency finder does not do this for a lib_deps library on its own. That
// -I is how Jet's per-frontend "#include \"JetConfig.hpp\"" resolves to this
// file instead of Jet's own JetConfig.example.hpp — see Jet's README,
// "Getting started".
//
// Tuned for the cabinet's 160x128 ST7735 panel: no Z-buffer, no buffered
// post-FX, painter's-algorithm sort. See JetConfig.example.hpp in the Jet
// source for what each flag does; only the values that differ from that
// template's own guidance are commented here.

// ---------------------------------------------------------------------------
// World-space scale
// ---------------------------------------------------------------------------

// 160x128 is well under the example's own "low-res" cutoff (480x320), where
// it recommends 4 over 8.
#define JET32_WORLD_SCALE 4

// ---------------------------------------------------------------------------
// Core rasterizer options
// ---------------------------------------------------------------------------

#define RENDER_TILE_BUFFER 0
#define TILE_WIDTH  32
#define TILE_HEIGHT 32

#define FAST_Z 1
#define LAZY_Z 0

#define SCREEN_DOOR_ALPHA 1

#define SKIP_ZERO_AREA_TRIANGLES 1

#define NOISE_ALPHA 0

// No Z-buffer: the cabinet's canvas is 160x128, and painter's-algorithm
// sorting (SORT_TRIANGLES below) is cheap enough at this triangle count.
#define Z_BUFFERING 0

#define SORT_TRIANGLES 1
#define SORT_SCENE_OBJECTS 0
#define SORT_SCENE_REVERSE 0

// Fades incoming fighters in from the fog instead of having them pop in at
// spawn distance.
#define DEPTH_ALPHA_BLEND 1

#define TEXTURE_MAPPING 0
#define PERSPECTIVE_CORRECT_TEXTURES 0
#define BILINEAR_FILTER 0

// Directional sun + ambient fill so enemy fighters read as 3D at this
// triangle count instead of flat silhouettes.
#define LIGHTING 1

#define Z_BRIGHTNESS 0

#define FLOAT_CAMERA_ANGLES 1
#define FLOAT_SIN_CACHE_SCALE 10
#define FLOAT_TAN_CACHE_SCALE 1

// ---------------------------------------------------------------------------
// Buffer layout
// ---------------------------------------------------------------------------

// The whole framebuffer is 160*128*2 = 40 KB either way — trivial next to
// the ESP32-S3's SRAM — so there's nothing to gain from halving resolution
// or splitting into interlaced fields. CombatFluxGame renders straight into
// the launcher's own GFXcanvas16 buffer, one canvas, no DMA field-swap.
#define HALF_WIDTH_BUFFERS 0
#define FIELD_BUFFERS 0
#define SSR_FIELD_REFLECT 0

// ---------------------------------------------------------------------------
// Post-processing effects
// ---------------------------------------------------------------------------

#define POSTFX_CRT         0
#define POSTFX_CELLSHADING 0

// All buffered post-FX stay off — JetConfig.example.hpp calls these
// "generally not viable on memory-constrained ESP32 targets", and the
// cabinet has no spare full-resolution buffer to give them.
#define POSTFX_ANTIALIASING 0
#define POSTFX_BLOOM        0
#define POSTFX_MOTION_BLUR  0
#define POSTFX_CHROMATIC    0
#define POSTFX_PIXELATE     0

#define CRT_SCANLINE_INTENSITY 48
#define MOTION_BLUR_STRENGTH   50
#define CHROMATIC_OFFSET        2
#define PIXELATE_SIZE           4
#define CELLSHADING_CELL_BITS   4

// ---------------------------------------------------------------------------
// Debug
// ---------------------------------------------------------------------------

#define DEBUG_OVERDRAW 0

// ---------------------------------------------------------------------------
// Depth / fog tuning (world-space units, scaled by JET32_WORLD_SCALE)
// ---------------------------------------------------------------------------

#define zBrightFar   (1600 * JET32_WORLD_SCALE)
#define zBrightNear  ( 200 * JET32_WORLD_SCALE)
#define zBrightScale 48

// These are in raw world units, NOT scaled by JET32_WORLD_SCALE like the
// template's defaults. The template's (8192 * 4) = 32768 put the fog band
// six times further out than either game's entire world is wide (~5000
// units), so DEPTH_ALPHA_BLEND never did anything at all.
//
// Sized to the actual play distances instead. Tank Flux's ground mesh is a
// single static mesh sized to cover the whole arena (ARENA_HALF=3000) plus
// this fog distance from any corner — see TankFluxGame.h's GROUND_SIZE
// comment — so the far value only needs to stay under Camera::farPlane
// (5200), not track a moving mesh edge anymore.
//
// Pushed out from an earlier 2500/3600: at that distance, most of the
// visible ground band (a low, near-horizontal first-person view covers a
// LOT of far terrain even close to the horizon) was fogging out to the
// near-black background gradient before the lit checkerboard mesh ever
// reached full opacity, making the world read as dark/uncoloured
// regardless of how bright the terrain's own lighting was tuned.
//
// Don't tighten this without a reason. DEPTH_ALPHA_BLEND fades by
// screen-door stipple, so a fogged object shows the ground through its gaps
// and reads as grey mush rather than its own colour. An earlier 1700/2500
// band started fading enemies before they even entered their own firing
// range, which made them near-invisible for the whole approach.
#define depthFogNear 3200
#define depthFogFar  5000

// ---------------------------------------------------------------------------
// Checkerboard rendering (desktop / high-res targets)
// ---------------------------------------------------------------------------

#define CHECKERBOARD_MODE 0
#define CHECKERBOARD_RECONSTRUCTION 0

// ---------------------------------------------------------------------------
// Screen-space picking
// ---------------------------------------------------------------------------

// Nothing in the cabinet uses screen-space picking — Tank Flux hit-tests by
// direct 3D distance instead (see TankFluxGame.h). MAX_PICK_QUERIES > 0
// tags every queued triangle with extra pick-attribution fields, which
// costs real render-queue memory for no benefit here — a contributing
// factor in a std::bad_alloc crash Tank Flux hit before its ground mesh
// was trimmed. Leave at 0 unless something actually calls
// Scene::setPickQueries/getPickResults.
#define MAX_PICK_QUERIES 0

// ---------------------------------------------------------------------------
// Platform detection
// ---------------------------------------------------------------------------

// PlatformIO's Arduino/ESP32 build already predefines ESP32 on the command
// line, so guard the definition — the template's unconditional #define
// causes a harmless but noisy "ESP32 redefined" warning on every Jet file.
#if defined(ESP_PLATFORM) && !defined(ESP32)
#define ESP32
#endif
