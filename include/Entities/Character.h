#pragma once

#include "raylib.h"
#include <Rendering/RenderData.h>

/// Everything an entity needs to advance one tick. Passed through the virtual
/// update() so subclasses share a single polymorphic entry point.
struct UpdateContext {
    float   dt         = 0.0f;
    Vector3 camForward = {0.0f, 0.0f, 1.0f};
    Vector3 camRight   = {1.0f, 0.0f, 0.0f};
};

class Character {
public:
    Character();
    virtual ~Character() = default;

    virtual void update(const UpdateContext &ctx) = 0;
    virtual CharacterRenderData getRenderData() const = 0;

    Vector3 getPosition() const{
        return position;
    }

    Vector3 getRotation() const{
        return rotation;
    }
    
protected:
    Vector3 position;
    Vector3 rotation;
};