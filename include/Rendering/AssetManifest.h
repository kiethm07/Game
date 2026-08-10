#pragma once
#include <Rendering/AssetID.h>

/// Which IEntityRenderer strategy an asset is drawn with.
enum class RendererKind {
  SkinnedCharacter, ///< SkinnedEntityRenderer (GPU-skinned GLB)
  DebugCube,        ///< DebugCubeRenderer (placeholder proxy)
};

struct AssetEntry {
  AssetID id;
  const char *modelPath; ///< nullptr → no model to load
  const char *animPath;  ///< nullptr → no animations to load
  RendererKind renderer = RendererKind::SkinnedCharacter;
  const AssetID *sharedModelId = nullptr; ///< if set, alias model to pointed-to source
  const AssetID *sharedAnimId = nullptr; ///< if set, alias animations to pointed-to source
};

static const AssetID kAshigaruSource = AssetID::PLAYER_WOLF;

static const AssetEntry kAssets[] = {
    {AssetID::PLAYER_WOLF, ASSET_DIR "/Paladin.rootmotion.glb",
     ASSET_DIR "/Paladin.rootmotion.glb", RendererKind::SkinnedCharacter},
    {AssetID::ENEMY_ASHIGARU, nullptr, nullptr, RendererKind::SkinnedCharacter,
     &kAshigaruSource, &kAshigaruSource},
};
