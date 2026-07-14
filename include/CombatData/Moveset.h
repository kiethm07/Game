#pragma once
#include <CombatData/Combo.h>
#include <unordered_map>
#include <string>

class Moveset {
public:
    Moveset() = default;
    ~Moveset() = default;

    void addCombo(const std::string& name, const Combo& combo);
    const Combo& getCombo(const std::string& name) const;

private:
    std::unordered_map<std::string, Combo> combo_map;
};
