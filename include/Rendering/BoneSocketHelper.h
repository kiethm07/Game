#pragma once
#include <raylib.h>
#include <Rendering/AssetManager.h>
#include <Rendering/RenderData.h>
#include <unordered_map>

/// Reads a point off an animated skeleton, in world space.
///
/// Everything a caller needs to hang something on a character comes from here:
/// the keyframe pair either side of the current time, the blend from the
/// outgoing clip, the root-motion cancellation that keeps the sample on the
/// mesh rather than where the mesh would be without it, and the model-to-world
/// transform. Getting any one of those wrong puts the attachment somewhere
/// plausible-looking that drifts, which is why there is one implementation of
/// it and sockets are named rather than open-coded.
class BoneSocketHelper {
public:
  BoneSocketHelper() = default;
  ~BoneSocketHelper() = default;

  /// Which named joint to sample. Each is a list of name fragments tried in
  /// order, so one entry covers the several naming conventions the rigs use.
  enum class Socket {
    RightHand, ///< The sword hand.
    Chest,     ///< Upper chest -- `mixamorig:Spine2` on these rigs.
  };

  /// One bone at the character's current animation time, plus what is needed to
  /// place points relative to it. Model space, except `draw_pos`.
  struct BoneSample {
    Vector3 translation;
    Quaternion rotation;
    /// Where the model is actually drawn: its world position with the clip's
    /// accumulated root motion taken back out.
    Vector3 draw_pos;
    Vector3 scale;
    float yaw;
  };

  /// Sample `socket` on this character. False if the rig has no such bone or
  /// the character has no animations, in which case `out` is untouched.
  static bool sampleSocket(AssetManager &assets,
                           const CharacterRenderData &render_data,
                           Socket socket, BoneSample &out);

  /// A point `local_offset` from the sampled bone, in that bone's own frame,
  /// converted to world space.
  static Vector3 pointOnBone(const BoneSample &sample, Vector3 local_offset);

  /// The sword's hilt and tip, from the hand bone. Convenience over the two
  /// calls above, and the original reason this class exists.
  static bool sampleSwordPoints(
      AssetManager &assets,
      const CharacterRenderData &render_data,
      Vector3 &out_base,
      Vector3 &out_tip,
      Vector3 blade_vector = {0.0f, 1.2f, 0.0f},
      Vector3 hilt_vector = {0.0f, 0.1f, 0.0f});

  /// The upper chest, in world space. What the deathblow marker rides on, so
  /// that it follows the body through every animation instead of hanging at a
  /// fixed height over the character's feet.
  static bool sampleChestPoint(AssetManager &assets,
                               const CharacterRenderData &render_data,
                               Vector3 &out_world,
                               Vector3 local_offset = {0.0f, 0.0f, 0.0f});

private:
  static int findBoneIndex(const Model &model, Socket socket);

  /// Resolved bone index per (asset, socket). A miss is cached as -1 too, so a
  /// rig without the joint costs one name scan rather than one per frame.
  static inline std::unordered_map<AssetID, int> hand_bone_cache;
  static inline std::unordered_map<AssetID, int> chest_bone_cache;
};
