#include <GameManager/StealthManager.h>

void StealthManager::update(const std::vector<Character*>& characters, Character* player, const std::vector<PhysicsObstacle>& obstacles, float dt) {
    if (!player) return;

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
            
            stealth.updateAwareness(max_detection, dt);
            if (max_detection > 0.0f) {
                stealth.setLastKnownPlayerPos(player->getPosition());
            }
        }
    }
}
