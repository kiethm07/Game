#pragma once
#include <Entities/Character.h>
#include <vector>
#include <utility>

class CombatManager {
public:
    void update(const std::vector<Character*>& characters);
    void drawDebug(const std::vector<Character*>& characters) const;

private:
    std::vector<std::pair<unsigned int, unsigned int>> active_hits;
    void clearHitsForAttacker(unsigned int attacker_id);
};