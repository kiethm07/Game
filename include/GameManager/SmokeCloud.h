#pragma once
#include <raylib.h>

class Character;

struct SmokeCloud {
    Vector3 position;
    float radius;
    float life;
    const Character* owner; // Used to determine immunity
};
