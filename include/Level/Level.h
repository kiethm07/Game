#pragma once

#include <Components/PhysicsObstacle.h>
#include <Entities/EnemyFactory.h>
#include <raylib.h>
#include <string>
#include <vector>

/// Where something starts the level, and which way it is facing.
struct SpawnPoint {
    Vector3 position{0.0f, 0.0f, 0.0f};

    /// Degrees about Y, same convention as PhysicsObstacle's yaw.
    float yaw = 0.0f;
};

struct EnemySpawn {
    EnemyType type = EnemyType::Swordman;
    SpawnPoint at{};
};

/// One map, as authored in Blender and exported by tools/export_level.py.
///
/// Deliberately a plain aggregate that owns no GPU resources: the visual mesh
/// is named here but loaded by the renderer, which already has the cache and
/// the shader setup for it. That keeps a Level cheap to construct, inspect in a
/// test, or build by hand — the old hardcoded arena is still expressible as a
/// few emplace_backs into `obstacles`.
struct Level {
    std::string name;

    /// Absolute path to the level's visual mesh, resolved against the JSON's
    /// own directory at load time. Empty when the level is collision-only,
    /// which is a valid state: a greybox is playable before any art exists.
    std::string visualModelPath;

    /// Everything the player can stand on or walk into. These feed physics,
    /// the navmesh bake, and stealth line-of-sight alike — the engine has one
    /// world-geometry primitive and this is it.
    std::vector<PhysicsObstacle> obstacles;

    SpawnPoint playerSpawn{};
    std::vector<EnemySpawn> enemySpawns;

    /// World extent of the level, covering visual geometry as well as
    /// collision. Drives the shadow map's far cascade, which is why it has to
    /// include a roof that casts but has no proxy of its own.
    BoundingBox bounds{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};

    /// False when loading failed. The level is then empty rather than
    /// half-built: a partially applied map is worse than none, because it
    /// looks playable while silently missing collision.
    bool valid = false;
};

namespace LevelLoader {

/// Parse a level.json written by tools/export_level.py.
///
/// Never throws and never returns a partially populated Level. On any problem
/// it logs the file, the offending entry, and what was wrong, then returns a
/// Level with `valid == false`.
Level load(const std::string &jsonPath);

} // namespace LevelLoader
