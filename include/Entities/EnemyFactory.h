#pragma once

#include <Entities/Enemy.h>
#include <Level/EnemySpawn.h>
#include <memory>

class EnemyFactory {
public:
    /// Build the enemy an authored spawn describes.
    ///
    /// Takes the whole spawn rather than a position and a type: the type is
    /// already in there, and so are the per-spawn overrides, which every
    /// concrete constructor resolves with value_or against its own defaults.
    static std::unique_ptr<Enemy> createEnemy(const EnemySpawn &spawn);
};
