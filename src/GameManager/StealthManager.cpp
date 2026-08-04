#include <GameManager/StealthManager.h>

void StealthManager::update(const std::vector<Character*>& characters, Character* player, const std::vector<PhysicsObstacle>& obstacles, float dt) {
    if (!player) return;

    std::vector<Enemy*> aware_enemies;

    for (Character* character : characters) {
        // We only care about enemies. For a larger game, an ECS system would be better.
        // Here we use dynamic_cast to check if it's an Enemy.
        Enemy* enemy = dynamic_cast<Enemy*>(character);
        if (enemy && !enemy->getStats().isDead()) {
            StealthComponent& stealth = enemy->getStealthComponent();
            
            float max_detection = 0.0f;
            for (const auto& sensor : stealth.getSensors()) {
                float str = sensor->getDetectionStrength(enemy, player, obstacles);
                if (str > max_detection) {
                    max_detection = str;
                }
            }
            
            if (max_detection > 0.0f) {
                stealth.setLastKnownPlayerPos(player->getPosition());
            }

            StealthState old_state = stealth.getStealthState();
            stealth.updateAwareness(max_detection, dt);
            StealthState new_state = stealth.getStealthState();
            
            if (new_state == StealthState::Aware) {
                aware_enemies.push_back(enemy);
            }
        }
    }

    // Swarm / Alert Propagation
    float alert_radius = 12.0f; // Fixed radius for alerting allies
    float alert_radius_sq = alert_radius * alert_radius;

    for (Enemy* alert_source : aware_enemies) {
        Vector3 source_pos = alert_source->getPosition();
        
        for (Character* character : characters) {
            Enemy* ally = dynamic_cast<Enemy*>(character);
            if (ally && ally != alert_source && !ally->getStats().isDead()) {
                // Only alert allies of the same faction (assuming enemies share faction logic)
                if (ally->getFaction() == alert_source->getFaction()) {
                    StealthComponent& ally_stealth = ally->getStealthComponent();
                    if (ally_stealth.getStealthState() != StealthState::Aware) {
                        float dist_sq = Vector3DistanceSqr(source_pos, ally->getPosition());
                        if (dist_sq <= alert_radius_sq) {
                            // Instantly alert the ally!
                            ally_stealth.forceAwareness(200.0f); 
                            ally_stealth.setLastKnownPlayerPos(player->getPosition());
                        }
                    }
                }
            }
        }
    }
}

void StealthManager::drawDebug(const std::vector<Character*>& characters) const {
    for (Character* character : characters) {
        Enemy* enemy = dynamic_cast<Enemy*>(character);
        if (enemy && !enemy->getStats().isDead()) {
            StealthComponent& stealth = enemy->getStealthComponent();
            for (const auto& sensor : stealth.getSensors()) {
                sensor->drawDebug(enemy);
            }
        }
    }
}
