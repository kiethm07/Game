#include <Entities/Player.h>
#include <cmath>
#include <raymath.h>

Player::Player(const InputManager& input_manager):
    input_manager(input_manager)
{ 
    position = {0, 0, 0};
}

void Player::update(float dt) {
    // Vector3 camForward = {0.0f, 0.0f, 1.0f};
    // Vector3 camRight = {1.0f, 0.0f, 0.0f};
    // update(dt, camForward, camRight);
}

void Player::update(float dt, Vector3 camForward, Vector3 camRight) {
    Vector3 moveDirection = calculateCameraRelativeDirection(camForward, camRight);

    if (moveDirection.x != 0.0f || moveDirection.z != 0.0f) {
        // Apply position displacement
        position.x += moveDirection.x * MOVEMENT_SPEED * dt;
        position.z += moveDirection.z * MOVEMENT_SPEED * dt;

        // Calculate the target angle based on the horizontal direction vector
        float target_yaw = std::atan2(moveDirection.x, moveDirection.z) * RAD2DEG;

        // Calculate the shortest angular distance
        float angle_diff = target_yaw - rotation.y;
        while (angle_diff < -180.0f) angle_diff += 360.0f;
        while (angle_diff > 180.0f)  angle_diff -= 360.0f;

        // FIX 1: Safeguard the interpolation factor (alpha) against dt spikes
        float alpha = ROTATION_SPEED * dt;
        if (alpha > 1.0f) alpha = 1.0f; // Ensures it never shoots past target_yaw mathematically

        // Fluidly interpolate rotation safely
        rotation.y += angle_diff * alpha;

        // FIX 2: Keep rotation.y cleanly wrapped within a standard 0-360 range 
        // to prevent floating-point inaccuracies over time
        while (rotation.y < 0.0f) rotation.y += 360.0f;
        while (rotation.y >= 360.0f) rotation.y -= 360.0f;
    }

    //Character::update(dt);
}

void Player::draw() const{
    Character::draw();
}

Vector3 Player::calculateCameraRelativeDirection(Vector3 camForward, Vector3 camRight) const {
    camForward.y = 0.0f;
    camRight.y = 0.0f;
    
    //Remove y for calculation
    camForward = Vector3Normalize(camForward);
    camRight = Vector3Normalize(camRight);

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

void Player::handleCombatAndUtilityInputs() {
    if (input_manager.isActionPressed(GameAction::ATTACK)) {
    }
    if (input_manager.isActionPressed(GameAction::DEFLECT)) {
    }
    if (input_manager.isActionPressed(GameAction::DODGE)) {
    }
    if (input_manager.isActionPressed(GameAction::LOCK_ON)) {
    }
}