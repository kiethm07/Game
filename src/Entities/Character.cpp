#include <Entities/Character.h>

Character::Character(){
    position = {0, 0, 0};
    rotation = {0, 0, 0};
}

void Character::draw() const{
    DrawCube(position, 1.0f, 1.0f, 1.0f, BLUE);
    DrawCubeWires(position, 1.0f, 1.0f, 1.0f, BLACK);
}