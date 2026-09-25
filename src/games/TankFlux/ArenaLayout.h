#ifndef TANK_ARENA_LAYOUT_H
#define TANK_ARENA_LAYOUT_H

#include <Arduino.h>
#include "TankFluxConfig.h"

// Where every obstacle, tree and repair kit sits, plus the obstacle
// collision queries. Pure data and maths: the game owns the 3D objects and
// moves them to match after generate().

namespace tankflux {

enum ObstacleShape : uint8_t { SHAPE_CUBE, SHAPE_PYRAMID, SHAPE_ROCK };
struct ObstacleDef { int32_t x, z, size; ObstacleShape shape; };
struct PointDef { int32_t x, z; };
struct Circle { float x, z, r; };

class ArenaLayout {
public:
    // Sizes and shapes are hand-tuned per slot and never change. Positions
    // are re-rolled by generate(); these starting positions are only used
    // if placement can't find room for a slot.
    ObstacleDef obstacles[OBSTACLE_COUNT] = {
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
    PointDef trees[TREE_COUNT] = {
        { -1000, -2000 }, {  2700,  2700 }, { -2700,  2700 },
        {  2700, -2700 }, {     0,  1250 }, {  2600,     0 },
    };
    PointDef kits[REPAIR_COUNT] = {
        {  2400,   600 }, { -2400,  -300 }, {   500, -2400 },
    };

    static constexpr int MAX_KEEP_CLEAR = 8;

    // Re-rolls every position, clear of the river, of each other, and of
    // the `keepClear` circles (the player and any live tanks).
    void generate(const Circle* keepClear, int keepClearCount);

    // Would a circle of `radius` at (x,z) overlap an obstacle?
    bool blocked(float x, float z, int32_t radius) const;

    // Moves (x,z) out of any obstacle it overlaps, for a circle of `radius`.
    void pushOut(float &x, float &z, int32_t radius) const;

    // A little under the half-diagonal, so you can just scrape past a corner.
    static int32_t obstacleRadius(const ObstacleDef &o) { return (o.size * 3) / 5; }

private:
    bool placeCircle(float radius, const Circle* placed, int placedCount,
                     float &outX, float &outZ) const;
};

}  // namespace tankflux

#endif  // TANK_ARENA_LAYOUT_H
