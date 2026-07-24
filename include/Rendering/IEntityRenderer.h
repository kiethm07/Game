#pragma once

#include <Rendering/AssetManager.h>
#include <Rendering/RenderData.h>

/// Abstract interface for per-entity rendering strategies.
/// Each entity type (skinned character, debug proxy, prop, etc.) implements
/// this interface. GameRenderer holds a map<AssetID, IEntityRenderer*> and
/// dispatches to the correct implementation without any if/else chain.
struct IEntityRenderer {
    virtual void draw(AssetManager &assets,
                      const CharacterRenderData &renderData) = 0;
    virtual ~IEntityRenderer() = default;
};
