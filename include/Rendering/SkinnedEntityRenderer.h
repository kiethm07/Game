#pragma once

#include <Rendering/IEntityRenderer.h>

/// Renders an entity that has a skinned GLB model and model animations.
/// Applies root-motion cancellation so the draw position matches the physics
/// position, then calls DrawModelEx for the final draw.
class SkinnedEntityRenderer : public IEntityRenderer {
public:
    void draw(AssetManager &assets,
              const CharacterRenderData &renderData,
              RenderPass pass = RenderPass::Scene) override;
};
