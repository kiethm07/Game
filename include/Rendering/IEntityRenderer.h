#pragma once

#include <Rendering/AssetManager.h>
#include <Rendering/RenderData.h>

/// Which pass an entity is being drawn for.
///
/// Every caster is drawn twice a frame: once into the shadow map from the
/// light, once into the scene from the camera. The two must agree exactly on
/// the pose, so implementations should branch as late as possible — ideally
/// only at the final draw call — rather than duplicating any of the transform
/// or animation work.
enum class RenderPass {
    Scene,       ///< Normal shaded draw into the active 3D scope.
    ShadowDepth, ///< Depth-only draw into the shadow map (see ShadowMap).
};

/// Abstract interface for per-entity rendering strategies.
/// Each entity type (skinned character, debug proxy, prop, etc.) implements
/// this interface. GameRenderer holds a map<AssetID, IEntityRenderer*> and
/// dispatches to the correct implementation without any if/else chain.
struct IEntityRenderer {
    virtual void draw(AssetManager &assets,
                      const CharacterRenderData &renderData,
                      RenderPass pass = RenderPass::Scene) = 0;
    virtual ~IEntityRenderer() = default;
};
