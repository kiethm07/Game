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

    // Both bosses are a Swordman for now: same animator, same attack data, by
    // request. They are not indistinguishable, though -- Enemy stores the
    // spawn's type, so `getType()` still answers MiniBoss here and the
    // phase-2 campfire gate reads that rather than guessing from stats or
    // position. MiniBoss also draws its own model (AssetID::ENEMY_MINIBOSS,
    // see AssetManifest.h) rather than the player's borrowed by everything
    // else built as a Swordman. Both bosses now draw their own model and
    // animate off their own clip pack.
    case EnemyType::MiniBoss:
        return std::make_unique<Swordman>(spawn, AssetID::ENEMY_MINIBOSS);
    case EnemyType::FinalBoss:
        return std::make_unique<Swordman>(spawn, AssetID::ENEMY_FINALBOSS);

    // Same shape as the two above, and for the same reason: a Swordman whose
    // AssetID is the whole of the difference. That one argument selects the
    // model, the clip-name table in SwordmanAnimator::descTable and, through
    // Swordman's constructor, the attack rotation.
    case EnemyType::Kimono_enemy:
        return std::make_unique<Swordman>(spawn, AssetID::ENEMY_KIMONO);
    }
    throw std::runtime_error("Unknown EnemyType in EnemyFactory");
}
