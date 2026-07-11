#pragma once

#include "raylib.h"

class Character {
public:
    Character();
    virtual ~Character() = default;

    virtual void update(float dt) = 0;
    virtual void draw() const;

    Vector3 getPosition() const{
        return position;
    }

    Vector3 getRoration() const{
        return rotation;
    }
    
protected:
    Vector3 position;
    Vector3 rotation;
};