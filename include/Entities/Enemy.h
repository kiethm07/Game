#pragma once

#include <Core/InputManager.h>
#include <Entities/Character.h>

class Enemy : public Character{
public:
    Enemy();
    ~Enemy() = default;

    void update(float dt) override;
    void update(float dt, Vector3 camForward, Vector3 camRight);
    CharacterRenderData getRenderData() const override;
    
private:
    const float MOVEMENT_SPEED = 5.0f;
    const float ROTATION_SPEED = 100.0f;
    int moveState = 0;
    
    Vector3 calculateCameraRelativeDirection(Vector3 camForward, Vector3 camRight) const;
    void handleCombatAndUtilityInputs();
};