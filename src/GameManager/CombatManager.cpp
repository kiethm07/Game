#include <GameManager/CombatManager.h>
#include <algorithm>
#include "raylib.h"
#include <Util/CollisionMath.h>
#include <cassert>

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
        assert(character);
        // 1. Draw HurtBoxes (Green Capsule Wireframes)
        auto hurtboxes = character->getHurtBoxes();
        for (const auto& hurtbox : hurtboxes) {
            Capsule capsule = hurtbox.getShape();
            DrawCapsuleWires(capsule.getBase(), capsule.getTip(), capsule.getRadius(), 8, 8, GREEN);
        }

        // 2. Draw Active HitBoxes (Red Sphere Wireframes)
        std::vector<HitBox> hitboxes = character->getActiveHitBoxes();
        for (const auto& hitbox : hitboxes) {
            if (hitbox.isSphere()) {
                Sphere sphere = hitbox.getSphere();
                DrawSphereWires(sphere.getCenter(), sphere.getRadius(), 10, 10, RED);
            } else if (hitbox.isCapsule()) {
                Capsule capsule = hitbox.getCapsule();
                DrawCapsuleWires(capsule.getBase(), capsule.getTip(), capsule.getRadius(), 8, 8, RED);
            }
        }
    }
}

#include <Rendering/ParticleManager.h>

void CombatManager::update(const std::vector<Character*>& characters, ParticleManager* particle_manager) {
    for (Character* attacker : characters) {
        assert(attacker);

        auto hitboxes = attacker->getActiveHitBoxes();

        if (hitboxes.empty()) {
            clearHitsForAttacker(attacker->getId());
            continue;
        }

        for (const auto& hitbox : hitboxes) {
            for (Character* defender : characters) {
                if (!defender || attacker->getId() == defender->getId()) continue;
                if (attacker->getFaction() == defender->getFaction()) continue;

                // If the attacker is executing a takedown, only hit the specific victim
                if (attacker->isExecuting() && !defender->isBeingExecuted()) {
                    continue;
                }

                // Skip if this swing already hit this target
                bool already_hit = std::any_of(active_hits.begin(), active_hits.end(),
                    [&](const auto& pair) {
                        return pair.first == attacker->getId() && pair.second == defender->getId();
                    });
                if (already_hit) continue;

                // Sphere/Capsule (HitBox) vs Capsule (HurtBox) collision check
                auto hurtboxes = defender->getHurtBoxes();
                for (const auto& hurtbox : hurtboxes) {
                    bool hit = false;
                    if (hitbox.isSphere()) {
                        hit = CollisionMath::checkSphereCapsule(hitbox.getSphere(), hurtbox.getShape());
                    } else if (hitbox.isCapsule()) {
                        hit = CollisionMath::checkCapsuleCapsule(hitbox.getCapsule(), hurtbox.getShape());
                    }

                    if (hit) {

                        DamageResult result = defender->takeDamage(hitbox.getHealthDamage(), hitbox.getPostureDamage(), attacker);
                        active_hits.push_back({ attacker->getId(), defender->getId() });
                        
                        if (particle_manager) {
                            if (result == DamageResult::BLOCKED) {
                                particle_manager->emitSparks(hitbox.getCenter(), 10);
                            } else if (result == DamageResult::PARRIED) {
                                particle_manager->emitSparks(hitbox.getCenter(), 50);
                            } else if (result == DamageResult::HIT) {
                                int blood_count = (hitbox.getHealthDamage() > 1000.0f) ? 100 : 30;
                                particle_manager->emitBlood(hitbox.getCenter(), blood_count);
                            }
                        }

                        //Only process one hit at a time for this specific hitbox
                        break;
                    }
                }
            }
        }
    }
}