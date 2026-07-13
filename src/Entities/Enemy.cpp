#include <Entities/Enemy.h>
#include <cmath>
#include <raymath.h>

Enemy::Enemy(const InputManager& input_manager):
    input_manager(input_manager)
{ 
    position = {10, 0, 10};
}

void Enemy::update(float dt) {
    // Vector3 camForward = {0.0f, 0.0f, 1.0f};
    // Vector3 camRight = {1.0f, 0.0f, 0.0f};
    // update(dt, camForward, camRight);
}

void Enemy::update(float dt, Vector3 camForward, Vector3 camRight) {

    position.x += MOVEMENT_SPEED * dt; // Move right along the X-axis
    position.z += MOVEMENT_SPEED * dt; // Move forward along the Z-axis
    //Character::update(dt);
}

void Enemy::draw() const{
    DrawCube(position, 1.0f, 1.0f, 1.0f, RED);
    DrawCubeWires(position, 1.0f, 1.0f, 1.0f, BLACK);
}

Vector3 Enemy::calculateCameraRelativeDirection(Vector3 camForward, Vector3 camRight) const {
    Vector3 direction = { 0.0f, 0.0f, 0.0f };
    if (input_manager.isActionHeld(GameAction::MOVE_FORWARD))  direction = Vector3Add(direction, camForward);
    if (input_manager.isActionHeld(GameAction::MOVE_BACKWARD)) direction = Vector3Subtract(direction, camForward);
    if (input_manager.isActionHeld(GameAction::MOVE_RIGHT))    direction = Vector3Add(direction, camRight);
    if (input_manager.isActionHeld(GameAction::MOVE_LEFT))     direction = Vector3Subtract(direction, camRight);

    if (direction.x != 0.0f || direction.z != 0.0f) {
        direction = Vector3Normalize(direction);
    }
    return direction;
}

void Enemy::handleCombatAndUtilityInputs() {
    if (input_manager.isActionPressed(GameAction::ATTACK)) {
    }
    if (input_manager.isActionPressed(GameAction::DEFLECT)) {
    }
    if (input_manager.isActionPressed(GameAction::DODGE)) {
    }
    if (input_manager.isActionPressed(GameAction::LOCK_ON)) {
    }
}