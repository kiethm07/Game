#include <Entities/EnemyFactory.h>
#include <Entities/Enemies/Swordman.h>
#include <stdexcept>

std::unique_ptr<Enemy> EnemyFactory::createEnemy(const EnemySpawn &spawn) {
    // No `default:` label, on purpose. With one, a row added to EnemyTypes.def
    // and forgotten here compiles cleanly and throws at level load; without
    // one, -Wswitch (on by default in clang) names the missing case at the
    // point the row was added. That warning is the whole intended cost of a new
    // type, so it is worth not suppressing.
    switch (spawn.type) {
    case EnemyType::Swordman:
        return std::make_unique<Swordman>(spawn);

    // Both bosses are a Swordman for now: same model, same animator, same
    // attack data, by request. They are not indistinguishable, though --
    // Enemy stores the spawn's type, so `getType()` still answers MiniBoss
    // here and the phase-2 campfire gate reads that rather than guessing from
    // stats or position. Giving them their own class later is a change to
    // these two lines.
    case EnemyType::MiniBoss:
    case EnemyType::FinalBoss:
        return std::make_unique<Swordman>(spawn);
    }
    throw std::runtime_error("Unknown EnemyType in EnemyFactory");
}
