#pragma once

#include <Rendering/IEntityRenderer.h>

/// Renders an entity as a coloured debug cube.
/// Used as a placeholder for enemies and other entities that don't yet have
/// a real model. Swap this out for a SkinnedEntityRenderer once the asset
/// and animations are ready.
class DebugCubeRenderer : public IEntityRenderer {
public:
  explicit DebugCubeRenderer(Color fillColor = BLUE, Color wireColor = BLACK);

  void draw(AssetManager &assets,
            const CharacterRenderData &renderData) override;

private:
  Color fillColor;
  Color wireColor;
};
