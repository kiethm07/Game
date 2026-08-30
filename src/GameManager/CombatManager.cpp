#include <GameManager/CombatManager.h>
#include <algorithm>
#include "raylib.h"
#include <Util/CollisionMath.h>
#include <cassert>
#include <GameManager/SoundController.h>
#include <Entities/Player.h>

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

void CombatManager::update(const std::vector<Character*>& characters, ParticleManager* particle_manager, SoundController* sound_controller) {
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
                            // On the defender's body, not at the hitbox's own
                            // centre.
                            //
                            // A hitbox is built from the ATTACKER's position
                            // plus a vertical offset (Player::getActiveHitBoxes),
                            // so its centre tracks the attacker's height. On
                            // flat ground that is close enough to the contact to
                            // pass for it; downhill it is not, and effects came
                            // out level with the attacker's chest -- over the
                            // defender's head. Clamping onto the hurtbox keeps
                            // the spawn inside the body that was actually hit,
                            // whatever the two are standing on.
                            //
                            // For a capsule hitbox the reference is the point on
                            // the weapon nearest the body rather than the middle
                            // of the weapon, which for a long swing are a blade's
                            // length apart.
                            Vector3 from = hitbox.getCenter();
                            if (hitbox.isCapsule()) {
                                const Capsule w = hitbox.getCapsule();
                                Vector3 on_weapon, on_body;
                                CollisionMath::closestPointsBetweenLineSegments(
                                    w.getBase(), w.getTip(),
                                    hurtbox.getShape().getBase(),
                                    hurtbox.getShape().getTip(),
                                    on_weapon, on_body);
                                from = on_weapon;
                            }
                            const Vector3 contact =
                                CollisionMath::contactPointOnCapsule(
                                    from, hurtbox.getShape());

                            // The ground under the thing that was hit. A
                            // character's position is its feet, so this is the
                            // surface it is standing on -- which is the height
                            // its blood should pool at.
                            const float floor_y = defender->getPosition().y + 0.05f;

                            if (result == DamageResult::BLOCKED) {
                                particle_manager->emitSparks(contact, 10, floor_y);
                            } else if (result == DamageResult::PARRIED) {
                                particle_manager->emitSparks(contact, 50, floor_y);
                            } else if (result == DamageResult::HIT) {
                                int blood_count = (hitbox.getHealthDamage() > 1000.0f) ? 100 : 30;
                                particle_manager->emitBlood(contact, blood_count, floor_y);
                            }
                        }

                        if (sound_controller) {
                            if (result == DamageResult::PARRIED) {
                                if (dynamic_cast<const Player*>(defender)) {
                                    sound_controller->playSFX(use_deflect_1 ? AssetID::SFX_DEFLECT_1 : AssetID::SFX_DEFLECT_2);
                                    use_deflect_1 = !use_deflect_1;
                                } else {
                                    sound_controller->playSFX(AssetID::SFX_DEFLECT_NPC);
                                }
                            } else if (result == DamageResult::BLOCKED) {
                                // Deliberately one sound for both sides, unlike
                                // the deflect above. A parry is the player's
                                // moment and earns a stereo pair plus its own
                                // NPC variant; a block is just the guard doing
                                // its job, and giving it the same treatment
                                // would make the two read as equals.
                                sound_controller->playSFX(AssetID::SFX_BLOCK);
                            } else if (result == DamageResult::HIT) {
                                if (!defender->isBeingExecuted()) {
                                    sound_controller->playSFX(AssetID::SFX_HIT);
                                }
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