#pragma once

#include <raylib.h>

/// Pure utility functions for skeletal animation with raylib.
/// These are stateless helpers with no dependency on any game class.
namespace AnimUtils {

/// Calls UpdateModelAnimation with fully bounds-checked indices.
/// Does nothing if anims is null, animCount is 0, or the keyframe count is 0.
void applyAnimationFrame(Model &model, ModelAnimation *anims, int animCount,
                         int animIndex, int frame);

/// Returns the corrected draw position for an entity whose animation contains
/// root motion baked into bone 0.
///
/// Reads the local XZ delta of bone 0 between frame 0 and the current frame,
/// rotates it into world space by the character's yaw, and subtracts it from
/// entityPos so the entity stays anchored to its physics position while the
/// skeleton animates forward.
///
/// @param anim       The active ModelAnimation.
/// @param frame      Current keyframe index (already bounds-checked by caller).
/// @param entityPos  The authoritative physics position of the entity.
/// @param yawDeg     Y-axis rotation of the entity in degrees.
/// @param modelScale Uniform scale factor applied to the model at draw time.
/// @return           Adjusted world-space draw position.
Vector3 cancelRootMotion(const ModelAnimation &anim, int frame,
                         Vector3 entityPos, float yawDeg, float modelScale = 1.0f);

} // namespace AnimUtils
