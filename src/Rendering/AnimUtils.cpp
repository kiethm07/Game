#include <Rendering/AnimUtils.h>
#include <raymath.h>
#include <cmath>

namespace AnimUtils {

bool hasRootMotion(const ModelAnimation &anim, float minTravel) {
    if (anim.boneCount <= 0 || anim.keyframeCount < 2) return false;

    // Furthest bone 0 ever gets from where it started. Comparing only the first
    // and last keyframe would misread looping clips: they close the loop by
    // returning the root to its origin, so a walk that travels 1.4m still ends
    // ~0 from its start. Peak excursion separates a stride from a weight shift.
    Vector3 origin = anim.keyframePoses[0][0].translation;
    float peakSq = 0.0f;

    for (int i = 1; i < anim.keyframeCount; i++) {
        Vector3 pose = anim.keyframePoses[i][0].translation;
        float dx = pose.x - origin.x;
        float dz = pose.z - origin.z;
        float distSq = dx * dx + dz * dz;
        if (distSq > peakSq) peakSq = distSq;
    }

    return peakSq > (minTravel * minTravel);
}

Vector3 cancelRootMotion(const ModelAnimation &anim, float frame,
                         Vector3 entityPos, float yawDeg, float modelScale) {
    if (anim.boneCount <= 0 || anim.keyframeCount <= 0) return entityPos;

    // In-place clips (idles, turns) move bone 0 only to sway. Cancelling that
    // would drag the whole model and slide the planted feet.
    if (!hasRootMotion(anim)) return entityPos;

    // Sample bone 0 the same way UpdateModelAnimation poses the skeleton: blend
    // between consecutive keyframes, wrapping the next index past the end. A
    // whole-frame offset against a blended skeleton leaves up to one frame of
    // travel uncancelled -- ~8cm per frame at running speed, seen as judder.
    // Self-guards the indices so callers can't index keyframePoses OOB.
    int current = static_cast<int>(frame);
    int next    = current + 1;
    float blend = frame - static_cast<float>(current);
    blend = Clamp(blend, 0.0f, 1.0f);

    current %= anim.keyframeCount;
    if (current < 0) current += anim.keyframeCount;
    next %= anim.keyframeCount;
    if (next < 0) next += anim.keyframeCount;

    // Bone 0 carries the forward root motion in local model space.
    // We cancel its XZ translation so the entity stays at its physics position.
    Vector3 rootFrame0 = anim.keyframePoses[0][0].translation;
    Vector3 rootFrameN = Vector3Lerp(anim.keyframePoses[current][0].translation,
                                     anim.keyframePoses[next][0].translation,
                                     blend);

    float localDeltaX = (rootFrameN.x - rootFrame0.x) * modelScale;
    float localDeltaZ = (rootFrameN.z - rootFrame0.z) * modelScale;

    // Rotate the local offset into world space by the character's facing angle.
    float yawRad   = yawDeg * DEG2RAD;
    float worldDeltaX =  localDeltaX * cosf(yawRad) + localDeltaZ * sinf(yawRad);
    float worldDeltaZ = -localDeltaX * sinf(yawRad) + localDeltaZ * cosf(yawRad);

    return { entityPos.x - worldDeltaX, entityPos.y, entityPos.z - worldDeltaZ };
}

} // namespace AnimUtils
