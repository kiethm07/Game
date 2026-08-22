#include <Rendering/BoneSocketHelper.h>
#include <Rendering/AnimUtils.h>
#include <Rendering/RootMotion.h>
#include <raymath.h>
#include <cstring>
#include <cmath>

namespace {

/// Name fragments for each socket, most specific first.
///
/// Substring matches rather than equality because the rigs prefix everything
/// with `mixamorig:`, and because the same joint is spelled three ways across
/// the exporters this project has taken models from.
struct SocketNames {
  const char *const *patterns;
  int count;
};

const char *const kHandNames[] = {"RightHand", "hand.r", "Hand_R"};

/// Spine2 is the upper chest on a Mixamo rig -- Hips, Spine, Spine1, Spine2,
/// Neck, Head. Ordered so a rig that stops at Spine1 still lands as high up the
/// back as it has, and `Spine` alone is the last resort rather than the first
/// match: it is the lowest of the three and would put the marker at the belt.
const char *const kChestNames[] = {"Spine2",  "UpperChest", "Spine1",
                                   "Chest",   "Spine"};

SocketNames namesFor(BoneSocketHelper::Socket socket) {
  switch (socket) {
  case BoneSocketHelper::Socket::Chest:
    return {kChestNames, (int)(sizeof(kChestNames) / sizeof(kChestNames[0]))};
  case BoneSocketHelper::Socket::RightHand:
  default:
    return {kHandNames, (int)(sizeof(kHandNames) / sizeof(kHandNames[0]))};
  }
}

} // namespace

int BoneSocketHelper::findBoneIndex(const Model &model, Socket socket) {
  const SocketNames names = namesFor(socket);
  // Pattern-major: an early pattern on a late bone beats a late pattern on an
  // early bone, which is what makes the kChestNames ordering mean anything.
  for (int p = 0; p < names.count; ++p) {
    for (int i = 0; i < model.skeleton.boneCount; ++i) {
      if (strstr(model.skeleton.bones[i].name, names.patterns[p]) != nullptr) {
        return i;
      }
    }
  }
  return -1;
}

bool BoneSocketHelper::sampleSocket(AssetManager &assets,
                                    const CharacterRenderData &render_data,
                                    Socket socket, BoneSample &out) {
  Model &model = assets.getModel(render_data.assetId);
  int anim_count = 0;
  ModelAnimation *anims = assets.getAnimations(render_data.assetId, anim_count);

  if (anims == nullptr || anim_count <= 0) {
    return false;
  }

  std::unordered_map<AssetID, int> &cache =
      (socket == Socket::Chest) ? chest_bone_cache : hand_bone_cache;

  int bone_index = -1;
  std::unordered_map<AssetID, int>::iterator it = cache.find(render_data.assetId);
  if (it != cache.end()) {
    bone_index = it->second;
  } else {
    bone_index = findBoneIndex(model, socket);
    cache[render_data.assetId] = bone_index;
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

  Vector3 bone_trans = Vector3Lerp(pose0.translation, pose1.translation, t);
  Quaternion bone_rot = QuaternionSlerp(pose0.rotation, pose1.rotation, t);

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

      Vector3 from_bone_trans = Vector3Lerp(from_pose0.translation, from_pose1.translation, from_t);
      Quaternion from_bone_rot = QuaternionSlerp(from_pose0.rotation, from_pose1.rotation, from_t);

      bone_trans = Vector3Lerp(from_bone_trans, bone_trans, blend);
      bone_rot = QuaternionSlerp(from_bone_rot, bone_rot, blend);
    }
  }

  // Root motion offset, so the sample sits on the mesh as drawn rather than
  // where the mesh would be if the clip carried no travel.
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

  out.translation = bone_trans;
  out.rotation = bone_rot;
  out.draw_pos = Vector3Subtract(render_data.transform.position, root_offset);
  out.scale = render_data.transform.scale;
  out.yaw = render_data.transform.rotation.y;
  return true;
}

Vector3 BoneSocketHelper::pointOnBone(const BoneSample &sample,
                                      Vector3 local_offset) {
  const Vector3 local = Vector3Add(
      sample.translation, Vector3RotateByQuaternion(local_offset, sample.rotation));
  const Vector3 scaled = Vector3Multiply(local, sample.scale);
  return Vector3Add(sample.draw_pos, RootMotion::toWorld(scaled, sample.yaw));
}

bool BoneSocketHelper::sampleSwordPoints(
    AssetManager &assets,
    const CharacterRenderData &render_data,
    Vector3 &out_base,
    Vector3 &out_tip,
    Vector3 blade_vector,
    Vector3 hilt_vector) {

  BoneSample hand;
  if (!sampleSocket(assets, render_data, Socket::RightHand, hand)) {
    return false;
  }

  out_base = pointOnBone(hand, hilt_vector);
  out_tip = pointOnBone(hand, blade_vector);
  return true;
}

bool BoneSocketHelper::sampleChestPoint(AssetManager &assets,
                                        const CharacterRenderData &render_data,
                                        Vector3 &out_world,
                                        Vector3 local_offset) {
  BoneSample chest;
  if (!sampleSocket(assets, render_data, Socket::Chest, chest)) {
    return false;
  }

  out_world = pointOnBone(chest, local_offset);
  return true;
}
