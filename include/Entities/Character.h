#pragma once

#include "raylib.h"
#include <Rendering/RenderData.h>

class Character {
public:
    Character();
    virtual ~Character() = default;

    virtual void update(float dt) = 0;
    virtual CharacterRenderData getRenderData() const = 0;

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