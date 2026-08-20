#pragma once
#include <raylib.h>
#include <Rendering/AssetManager.h>
#include <Rendering/RenderData.h>
#include <unordered_map>

class BoneSocketHelper {
public:
  BoneSocketHelper() = default;
  ~BoneSocketHelper() = default;

  static bool sampleSwordPoints(
      AssetManager &assets,
      const CharacterRenderData &render_data,
      Vector3 &out_base,
      Vector3 &out_tip,
      Vector3 blade_vector = {0.0f, 1.2f, 0.0f},
      Vector3 hilt_vector = {0.0f, 0.1f, 0.0f});

private:
  static int findHandBoneIndex(const Model &model, const ModelAnimation *anims, int anim_count);
  static inline std::unordered_map<AssetID, int> bone_index_cache;
};
