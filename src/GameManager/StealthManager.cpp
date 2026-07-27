#include <GameManager/StealthManager.h>

void StealthManager::update(const std::vector<Character*>& characters, const Vector3& player_pos) {
    for (Character* character : characters) {
        // We only care about enemies. For a larger game, an ECS system would be better.
        // Here we use dynamic_cast to check if it's an Enemy.
        Enemy* enemy = dynamic_cast<Enemy*>(character);
        if (enemy && !enemy->getStats().isDead()) {
            StealthComponent& stealth = enemy->getStealthComponent();
            Vector3 enemy_pos = enemy->getPosition();
            
            bool detected = false;
            for (const auto& sensor : stealth.getSensors()) {
                if (sensor->checkDetection(enemy_pos, player_pos)) {
                    detected = true;
                    break;
                }
            }
            
            stealth.setPlayerDetected(detected);
        }
    }
}
