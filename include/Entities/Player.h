#pragma once

#include <Core/InputManager.h>
#include <Components/CombatComponent.h>
#include <Entities/Character.h>

class Player : public Character{
public:
    Player(const InputManager& input_manager);
    ~Player() = default;

    void update(float dt) override;
    void update(float dt, Vector3 camForward, Vector3 camRight);
    void draw() const override;
    
private:
    const InputManager& input_manager;
    const float MOVEMENT_SPEED = 5.0f;
    const float ROTATION_SPEED = 5.0f;
    CombatComponent combat_component;
    Combo combo;
    
    Vector3 calculateCameraRelativeDirection(Vector3 camForward, Vector3 camRight) const;
    void handleCombatAndUtilityInputs();
};