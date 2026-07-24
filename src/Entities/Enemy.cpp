#include <Entities/Enemy.h>
#include <raymath.h>

Enemy::Enemy()
{ 
    position = {5, 0, 5};
}

void Enemy::update(const UpdateContext &ctx) {
    const float dt = ctx.dt;
    // Advance framerate-independent playback time.
    animTime += dt;

    moveState++;
    bool isMoving = false;
    if (moveState % 100 < 50) {
        position.x += MOVEMENT_SPEED * dt;
        position.z += MOVEMENT_SPEED * dt;
        isMoving = true;
    } 
    else {
        position.x -= MOVEMENT_SPEED * dt;
        position.z -= MOVEMENT_SPEED * dt;
        isMoving = true;
    }
    
    // In this basic AI, we just say it's always moving right now
    // based on the logic above, but let's wire it correctly.
    int targetAnimIndex = isMoving ? static_cast<int>(Enemy::AnimState::WALK) : static_cast<int>(Enemy::AnimState::IDLE);
    
    if (currentAnimIndex != targetAnimIndex) {
        currentAnimIndex = targetAnimIndex;
        animTime = 0.0f;
    }
}

CharacterRenderData Enemy::getRenderData() const {
    return {
        AssetID::ENEMY_ASHIGARU,
        /*transform*/ { position, rotation, {1.0f, 1.0f, 1.0f} },
        /*animation*/ { currentAnimIndex, animTime }
    };
}

Vector3 Enemy::calculateCameraRelativeDirection(Vector3 camForward, Vector3 camRight) const {
    Vector3 direction = { 0.0f, 0.0f, 0.0f };
    return direction;
}

void Enemy::handleCombatAndUtilityInputs() {

}