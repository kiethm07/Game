#pragma once
#include <Components/Faction.h>

class FactionRelations {
public:
    static bool areHostile(Faction attacker, Faction target) {
        // if (attacker == Faction::PLAYER && target != Faction::PLAYER) return true;
        // if (target == Faction::PLAYER && attacker != Faction::PLAYER) return true;
        if (attacker == target) return false;
        return true;
    }
};