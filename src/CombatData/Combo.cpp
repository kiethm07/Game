#include <CombatData/Combo.h>
#include <stdexcept>

void Combo::addAttack(AttackID id) {
    attack_chain.push_back(id);
}

bool Combo::isEmpty() const {
    return attack_chain.empty();
}

int Combo::getAttackCount() const {
    return attack_chain.size();
}

AttackID Combo::getAttackID(int index) const {
    if (index >= attack_chain.size()) {
        throw std::out_of_range("Combo step execution index out of bounds.");
    }
    return attack_chain[index];
}