#include <Entities/Enemy.h>
#include <raymath.h>

Enemy::Enemy()
{ 
    position = {5, 0, 5};
}

void Enemy::update(float dt) {

}

void Enemy::update(float dt, Vector3 camForward, Vector3 camRight) {
    moveState++;
    if (moveState % 100 < 50) {
        position.x += MOVEMENT_SPEED * dt;
        position.z += MOVEMENT_SPEED * dt;
    } 
    else {
        position.x -= MOVEMENT_SPEED * dt;
        position.z -= MOVEMENT_SPEED * dt;
    }
    //Character::update(dt);
}

CharacterRenderData Enemy::getRenderData() const {
    return {
        AssetID::ENEMY_ASHIGARU,
        0,
        0,
        position,
        rotation,
        {1.0f, 1.0f, 1.0f}
    };
}

Vector3 Enemy::calculateCameraRelativeDirection(Vector3 camForward, Vector3 camRight) const {
    Vector3 direction = { 0.0f, 0.0f, 0.0f };
    return direction;
}

void Enemy::handleCombatAndUtilityInputs() {

}