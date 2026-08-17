#include <Rendering/BoneSocketHelper.h>
#include <Rendering/AnimUtils.h>
#include <Rendering/RootMotion.h>
#include <raymath.h>
#include <cstring>
#include <cmath>

int BoneSocketHelper::findHandBoneIndex(const Model &model, const ModelAnimation *anims, int anim_count) {
  for (int i = 0; i < model.skeleton.boneCount; ++i) {
    const char *name = model.skeleton.bones[i].name;
    if (strstr(name, "RightHand") != nullptr || strstr(name, "hand.r") != nullptr || strstr(name, "Hand_R") != nullptr) {
      return i;
    }
  }
  return -1;
}

bool BoneSocketHelper::sampleSwordPoints(
    AssetManager &assets,
    const CharacterRenderData &render_data,
    Vector3 &out_base,
    Vector3 &out_tip,
    Vector3 blade_vector,
    Vector3 hilt_vector) {
  
  Model &model = assets.getModel(render_data.assetId);
  int anim_count = 0;
  ModelAnimation *anims = assets.getAnimations(render_data.assetId, anim_count);

  if (anims == nullptr || anim_count <= 0) {
    return false;
  }

  int bone_index = -1;
  std::unordered_map<AssetID, int>::iterator it = bone_index_cache.find(render_data.assetId);
  if (it != bone_index_cache.end()) {
    bone_index = it->second;
  } else {
    bone_index = findHandBoneIndex(model, anims, anim_count);
    bone_index_cache[render_data.assetId] = bone_index;
  }

  if (bone_index < 0) {
    return false;
  }

  const int anim_index = render_data.animation.animIndex;
  const float frame = render_data.animation.animTime * AnimUtils::ANIM_SAMPLE_RATE;

  auto wrapIndex = [anim_count](int index) {
    int wrapped = index % anim_count;
    if (wrapped < 0) {
      wrapped += anim_count;
    }
    return wrapped;
  };

  const int wrapped_anim = wrapIndex(anim_index);
  const ModelAnimation &anim = anims[wrapped_anim];

  if (bone_index >= anim.boneCount || anim.keyframeCount <= 0 || anim.keyframePoses == nullptr) {
    return false;
  }

  int f0 = static_cast<int>(std::floor(frame)) % anim.keyframeCount;
  if (f0 < 0) {
    f0 += anim.keyframeCount;
  }
  int f1 = (f0 + 1) % anim.keyframeCount;
  float t = frame - std::floor(frame);

  Transform pose0 = anim.keyframePoses[f0][bone_index];
  Transform pose1 = anim.keyframePoses[f1][bone_index];

  Vector3 hand_trans = Vector3Lerp(pose0.translation, pose1.translation, t);
  Quaternion hand_rot = QuaternionSlerp(pose0.rotation, pose1.rotation, t);

  const int from_index = render_data.animation.blendFromIndex;
  const float from_frame = render_data.animation.blendFromTime * AnimUtils::ANIM_SAMPLE_RATE;
  const float blend = render_data.animation.blend;
  const bool blending = (from_index >= 0) && (blend < 1.0f);

  if (blending) {
    const int wrapped_from = wrapIndex(from_index);
    const ModelAnimation &from_anim = anims[wrapped_from];
    if (bone_index < from_anim.boneCount && from_anim.keyframeCount > 0 && from_anim.keyframePoses != nullptr) {
      int from_f0 = static_cast<int>(std::floor(from_frame)) % from_anim.keyframeCount;
      if (from_f0 < 0) {
        from_f0 += from_anim.keyframeCount;
      }
      int from_f1 = (from_f0 + 1) % from_anim.keyframeCount;
      float from_t = from_frame - std::floor(from_frame);

      Transform from_pose0 = from_anim.keyframePoses[from_f0][bone_index];
      Transform from_pose1 = from_anim.keyframePoses[from_f1][bone_index];

      Vector3 from_hand_trans = Vector3Lerp(from_pose0.translation, from_pose1.translation, from_t);
      Quaternion from_hand_rot = QuaternionSlerp(from_pose0.rotation, from_pose1.rotation, from_t);

      hand_trans = Vector3Lerp(from_hand_trans, hand_trans, blend);
      hand_rot = QuaternionSlerp(from_hand_rot, hand_rot, blend);
    }
  }

  // Calculate local model-space positions
  Vector3 local_base = Vector3Add(hand_trans, Vector3RotateByQuaternion(hilt_vector, hand_rot));
  Vector3 local_tip = Vector3Add(hand_trans, Vector3RotateByQuaternion(blade_vector, hand_rot));

  // Compute root motion offset to match mesh position exactly
  auto rootOffset = [&assets, &render_data](int index, float at_frame) {
    const RootMotion::Track &track = assets.getRootMotion(render_data.assetId, index);
    if (!track.hasMotion) {
      return Vector3{0.0f, 0.0f, 0.0f};
    }
    return RootMotion::sampleOffset(track, at_frame);
  };

  Vector3 root_offset = rootOffset(anim_index, frame);
  if (blending) {
    root_offset = Vector3Lerp(rootOffset(from_index, from_frame), root_offset, blend);
  }
  root_offset = Vector3Multiply(root_offset, render_data.transform.scale);
  root_offset = RootMotion::toWorld(root_offset, render_data.transform.rotation.y);

  Vector3 draw_pos = Vector3Subtract(render_data.transform.position, root_offset);

  // Transform model space to world space
  Vector3 scaled_base = Vector3Multiply(local_base, render_data.transform.scale);
  Vector3 scaled_tip = Vector3Multiply(local_tip, render_data.transform.scale);

  out_base = Vector3Add(draw_pos, RootMotion::toWorld(scaled_base, render_data.transform.rotation.y));
  out_tip = Vector3Add(draw_pos, RootMotion::toWorld(scaled_tip, render_data.transform.rotation.y));

  return true;
}
