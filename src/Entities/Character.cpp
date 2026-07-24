#include <Entities/Character.h>
#include <rlgl.h>

Character::Character(Faction faction) : faction(faction) {
  id = next_id++;
  position = {0, 0, 0};
  rotation = {0, -180, 0};
}

void Character::draw() const {
  DrawCube(position, 1.0f, 1.0f, 1.0f, BLUE);
  DrawCubeWires(position, 1.0f, 1.0f, 1.0f, BLACK);
}