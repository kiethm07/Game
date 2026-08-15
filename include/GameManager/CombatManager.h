#pragma once
#include <Entities/Character.h>
#include <vector>
#include <utility>

class ParticleManager;
class SoundController;

class CombatManager {
public:
    void update(const std::vector<Character*>& characters, ParticleManager* particle_manager = nullptr, SoundController* sound_controller = nullptr);
    void drawDebug(const std::vector<Character*>& characters) const;

private:
    std::vector<std::pair<unsigned int, unsigned int>> active_hits;
    bool use_deflect_1 = true;
    void clearHitsForAttacker(unsigned int attacker_id);
};