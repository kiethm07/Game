#pragma once
#include <CombatData/AttackId.h>
#include <vector>

class Combo {
public:
    Combo() = default;
    Combo(std::initializer_list<AttackID> ids) : attack_chain(ids) {}
    ~Combo() = default;

    void addAttack(AttackID id);
    bool isEmpty() const;
    int getAttackCount() const;
    AttackID getAttackID(int index) const;

private:
    std::vector<AttackID> attack_chain;
};