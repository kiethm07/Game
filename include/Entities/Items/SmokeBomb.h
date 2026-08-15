#pragma once
#include <Entities/Items/Item.h>
#include <Entities/Player.h>

class SmokeBomb : public Item {
public:
    SmokeBomb(int starting_charges = 3) : Item("Smoke Bomb", starting_charges, 0.5f) {}

    void use(Player* player) override {
        // Drop smoke bomb at player's feet
        player->spawnSmokeCloud(player->getPosition(), 4.0f, 6.0f);
    }
};
