#pragma once

#include <Entities/EnemyTypes.h>
#include <optional>
#include <raylib.h>

/// Where something starts the level, and which way it is facing.
struct SpawnPoint {
    Vector3 position{0.0f, 0.0f, 0.0f};

    /// Degrees about Y, same convention as PhysicsObstacle's yaw.
    float yaw = 0.0f;
};

/// Per-spawn tuning from assets/levels/<phase>/enemies.json.
///
/// Every field is optional, and absent means "whatever this type's constructor
/// already sets" -- never a number restated here. That is the whole reason
/// these are std::optional rather than floats pre-filled with the swordman's
/// values: the defaults stay in Swordman's constructor, next to the code that
/// reads them, and retuning one touches neither this header, nor the loader,
/// nor the schema.
struct EnemyOverrides {
    /// Full health as well as the cap. Stats' constructor starts a character at
    /// max, so this makes a weaker enemy, not a wounded one -- which is why the
    /// JSON key is `maxHealth` and not `health`.
    std::optional<float> maxHealth;

    /// VisionSensor's two arguments. The cone is the full angle in degrees.
    std::optional<float> visionRadius;
    std::optional<float> visionConeDegrees;

    /// Seeds StealthComponent's 0-200 awareness scale (100 = Suspicious,
    /// 200 = Aware).
    ///
    /// A head start, not a permanent state. StealthManager runs updateAwareness
    /// every frame regardless of what seeded it, so an enemy that starts at 200
    /// and never actually sees the player decays back to Unaware about eleven
    /// seconds later (memory_time 3 s, then decay_rate 25/s). What it buys is an
    /// enemy that comes looking, not one that is permanently hostile.
    std::optional<float> startAwareness;
};

/// One enemy, as authored.
struct EnemySpawn {
    EnemyType type = EnemyType::Swordman;
    SpawnPoint at{};

    /// False when the overlay omitted `y`, meaning `at.position.y` is a
    /// placeholder that has to be replaced with the ground under (x, z) before
    /// anything is built there. Always true for a spawn out of level.json,
    /// whose Blender marker always carried a height.
    ///
    /// Resolved by GameplayState rather than by LevelLoader, because answering
    /// it needs the collision mesh -- which a Level only ever names.
    bool hasExplicitY = true;

    EnemyOverrides overrides{};
};
