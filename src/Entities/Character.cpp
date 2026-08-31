#include <Entities/Character.h>
#include <rlgl.h>

Character::Character(Faction faction) : faction(faction) {
  id = next_id++;
  position = {0, 0, 0};
  rotation = {0, -180, 0};
}

void Character::draw() const {
  rlPushMatrix(); // Record the current world matrix

  rlTranslatef(position.x, position.y, position.z);

  rlRotatef(rotation.y, 0.0f, 1.0f, 0.0f);

  DrawCube({0.0f, 0.0f, 0.0f}, 1.0f, 1.0f, 1.0f, BLUE);
  DrawCubeWires({0.0f, 0.0f, 0.0f}, 1.0f, 1.0f, 1.0f, BLACK);

  rlPopMatrix(); // Restore the previous world matrix
}