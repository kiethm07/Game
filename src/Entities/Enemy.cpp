#include <Entities/Enemy.h>
#include <cmath>
#include <raymath.h>
#include <random>

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

void Enemy::draw() const{
    DrawCube(position, 1.0f, 1.0f, 1.0f, RED);
    DrawCubeWires(position, 1.0f, 1.0f, 1.0f, BLACK);
}

Vector3 Enemy::calculateCameraRelativeDirection(Vector3 camForward, Vector3 camRight) const {
    Vector3 direction = { 0.0f, 0.0f, 0.0f };
    return direction;
}

void Enemy::handleCombatAndUtilityInputs() {

}