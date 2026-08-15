#pragma once
#include <Entities/Items/Item.h>
#include <Entities/Player.h>

class HealingGourd : public Item {
public:
    HealingGourd(int initial_charges = 3)
        // 1.5 seconds to drink
        : Item("Healing Gourd", initial_charges, 1.5f) {}

    void use(Player* player) override {
        // Heal 50% of max health
        float heal_amount = player->getStats().getMaxHealth() * 0.5f;
        // The stats system applyDamage handles healing if damage is negative
        player->getMutableStats().applyDamage(-heal_amount, 0.0f);
    }
};
