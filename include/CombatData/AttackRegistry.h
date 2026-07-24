// CombatData/AttackRegistry.h
#pragma once
#include <CombatData/AttackData.h>
#include <CombatData/AttackId.h>
#include <unordered_map>
#include <string>

class AttackRegistry {
private:
    std::unordered_map<AttackID, AttackData> attack_catalog;
    
    //Singleton pattern
    AttackRegistry() {
        InitializeCatalog();
    }
    AttackRegistry(const AttackRegistry&) = delete;
    AttackRegistry(AttackRegistry&&) = delete;
    AttackRegistry& operator=(const AttackRegistry&) = delete;
    AttackRegistry& operator=(AttackRegistry&&) = delete;

    void InitializeCatalog();
    
public:
    static AttackRegistry& instance() {
        static AttackRegistry instance;
        return instance;
    }

    const AttackData& getAttackData(AttackID id) const {
        return attack_catalog.at(id);
    }
};