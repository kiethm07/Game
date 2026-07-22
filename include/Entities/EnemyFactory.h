#pragma once

#include <Entities/Enemy.h>
#include <memory>

enum class EnemyType {
    Swordman
};

class EnemyFactory {
public:
    static std::unique_ptr<Enemy> createEnemy(EnemyType type, Vector3 position);
};