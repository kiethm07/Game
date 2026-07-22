#include <Entities/EnemyFactory.h>
#include <Entities/Enemies/Swordman.h>
#include <stdexcept>

std::unique_ptr<Enemy> EnemyFactory::createEnemy(EnemyType type, Vector3 position) {
    switch (type) {
    case EnemyType::Swordman:
        return std::make_unique<Swordman>(position);
    default:
        throw std::runtime_error("Unknown EnemyType in EnemyFactory");
    }
}