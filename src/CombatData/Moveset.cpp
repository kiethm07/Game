#include <CombatData/Moveset.h>
#include <stdexcept>

void Moveset::addCombo(const std::string& name, const Combo& combo) {
    combo_map[name] = combo;
}

const Combo& Moveset::getCombo(const std::string& name) const {
    auto it = combo_map.find(name);
    if (it == combo_map.end()) {
        throw std::runtime_error("Moveset: Combo '" + name + "' not found.");
    }
    return it->second;
}