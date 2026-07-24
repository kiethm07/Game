#include <Rendering/AnimUtils.h>
#include <raymath.h>
#include <cmath>

namespace AnimUtils {

void applyAnimationFrame(Model &model, ModelAnimation *anims, int animCount,
                         int animIndex, int frame) {
    if (anims == nullptr || animCount <= 0) return;
    int safeAnimIndex = animIndex % animCount;
    int keyframeCount = anims[safeAnimIndex].keyframeCount;
    if (keyframeCount <= 0) return;
    int safeFrame = frame % keyframeCount;
    UpdateModelAnimation(model, anims[safeAnimIndex], safeFrame);
}

Vector3 cancelRootMotion(const ModelAnimation &anim, int frame,
                         Vector3 entityPos, float yawDeg, float modelScale) {
    if (anim.boneCount <= 0 || anim.keyframeCount <= 0) return entityPos;

    // Bone 0 carries the forward root motion in local model space.
    // We cancel its XZ translation so the entity stays at its physics position.
    Vector3 rootFrame0 = anim.keyframePoses[0][0].translation;
    Vector3 rootFrameN = anim.keyframePoses[frame][0].translation;

    float localDeltaX = (rootFrameN.x - rootFrame0.x) * modelScale;
    float localDeltaZ = (rootFrameN.z - rootFrame0.z) * modelScale;

    // Rotate the local offset into world space by the character's facing angle.
    float yawRad   = yawDeg * DEG2RAD;
    float worldDeltaX =  localDeltaX * cosf(yawRad) + localDeltaZ * sinf(yawRad);
    float worldDeltaZ = -localDeltaX * sinf(yawRad) + localDeltaZ * cosf(yawRad);

    return { entityPos.x - worldDeltaX, entityPos.y, entityPos.z - worldDeltaZ };
}

} // namespace AnimUtils
