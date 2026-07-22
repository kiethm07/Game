#include <Entities/Enemy.h>

Enemy::Enemy(Vector3 start_position, Faction faction)
    : Character(faction)
{
    position = start_position;
    rotation = { 0.0f, 0.0f, 0.0f };
}