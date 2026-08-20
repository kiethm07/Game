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

    /// Absolute path to the level's collision mesh, or empty when the level
    /// collides purely against `obstacles`.
    ///
    /// Named here but loaded elsewhere, exactly as `visualModelPath` is: the
    /// only loader available reads a .glb through raylib, which needs a GL
    /// context, and keeping that out of LevelLoader is what lets a Level still
    /// be parsed and inspected without a window.
    std::string collisionMeshPath;

    /// Absolute path to the level's detail mesh — scenery that is only ever
    /// drawn — or empty when the level ships none. Grass, at the time of
    /// writing.
    ///
    /// Separate from `visualModelPath` for a reason that is not filing. It is
    /// drawn in the scene pass and skipped in the depth pass, so nothing in it
    /// casts a shadow and none of its triangles reach either cascade — and
    /// that separation is only expressible as a second model, because raylib's
    /// glTF loader discards mesh and material names and leaves nothing to
    /// identify the grass by inside a combined one. Nothing here collides, and
    /// nothing here contributes to `bounds`.
    std::string detailModelPath;

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
