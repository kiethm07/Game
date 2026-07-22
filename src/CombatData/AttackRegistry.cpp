#include <CombatData/AttackRegistry.h>

void AttackRegistry::InitializeCatalog() {
    // attack_catalog.emplace(AttackID::PlayerLight1,          AttackData(0.15f, 0.10f, 0.25f));
    // attack_catalog.emplace(AttackID::PlayerLight2,          AttackData(0.12f, 0.12f, 0.30f));
    // attack_catalog.emplace(AttackID::PlayerHeavyFinisher,   AttackData(0.30f, 0.18f, 0.55f));

    ///Debug purpose, comment after use
    attack_catalog.emplace(AttackID::PlayerLight1,          AttackData(1.0f, 1.0f, 1.0f));
    attack_catalog.emplace(AttackID::PlayerLight2,          AttackData(1.0f, 1.0f, 1.0f));
    attack_catalog.emplace(AttackID::PlayerHeavyFinisher,   AttackData(1.0f, 1.0f, 1.0f));
}