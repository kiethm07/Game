#include <GameManager/CombatManager.h>
#include <algorithm>
#include "raylib.h"

void CombatManager::clearHitsForAttacker(unsigned int attacker_id) {
    for (size_t i = 0; i < active_hits.size(); ) {
        if (active_hits[i].first == attacker_id) {
            active_hits.erase(active_hits.begin() + i);
        }
        else {
            ++i;
        }
    }
}

void CombatManager::drawDebug(const std::vector<Character*>& characters) const {
    for (const auto* character : characters) {
        if (!character) continue;

        // 1. Draw HurtBoxes (Green wireframes)
        HurtBox hurtbox = character->getHurtBox();
        DrawSphereWires(hurtbox.getCenter(), hurtbox.getRadius(), 8, 8, GREEN);

        // 2. Draw Active HitBoxes (Red wireframes)
        std::vector<HitBox> hitboxes = character->getActiveHitBoxes();
        for (const auto& hitbox : hitboxes) {
            DrawSphereWires(hitbox.getCenter(), hitbox.getRadius(), 10, 10, RED);
        }
    }
}

void CombatManager::update(const std::vector<Character*>& characters) {
    for (Character* attacker : characters) {
        if (!attacker) continue;

        auto hitboxes = attacker->getActiveHitBoxes();

        if (hitboxes.empty()) {
            clearHitsForAttacker(attacker->getId());
            continue;
        }

        for (const auto& hitbox : hitboxes) {
            for (Character* defender : characters) {
                if (!defender || attacker->getId() == defender->getId()) continue;
                if (attacker->getFaction() == defender->getFaction()) continue;

                // Skip if this swing already hit this target
                bool already_hit = std::any_of(active_hits.begin(), active_hits.end(),
                    [&](const auto& pair) {
                        return pair.first == attacker->getId() && pair.second == defender->getId();
                    });
                if (already_hit) continue;

                // Sphere collision check
                if (CheckCollisionSpheres(hitbox.getCenter(), hitbox.getRadius(),
                    defender->getHurtBox().getCenter(), defender->getHurtBox().getRadius())) {

                    defender->takeDamage(hitbox.getHealthDamage(), hitbox.getPostureDamage());
                    active_hits.push_back({ attacker->getId(), defender->getId() });
                }
            }
        }
    }
}