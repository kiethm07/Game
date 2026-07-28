#include <Rendering/AnimUtils.h>
#include <Rendering/RootMotion.h>
#include <Rendering/SkinnedEntityRenderer.h>
#include <raymath.h>

void SkinnedEntityRenderer::draw(AssetManager &assets,
                                 const CharacterRenderData &renderData) {
    // 1. Fetch model and animations from the asset manager.
    Model &model   = assets.getModel(renderData.assetId);
    int animCount  = 0;
    ModelAnimation *anims = assets.getAnimations(renderData.assetId, animCount);

    const int   animIndex = renderData.animation.animIndex;
    // Derive the (fractional) keyframe from framerate-independent playback time.
    const float frame = renderData.animation.animTime * AnimUtils::ANIM_SAMPLE_RATE;

    // 2. Drive the skeleton. With SUPPORT_GPU_SKINNING enabled (see CMakeLists),
    //    UpdateModelAnimation only refreshes bone matrices — no CPU vertex
    //    skinning — and the skinning shader (attached by AssetManager) does the
    //    vertex work on the GPU, so entities can share one Model cheaply.
    Vector3 drawPosition = renderData.transform.position;
    if (anims != nullptr && animCount > 0) {
        // findAnimation() returns -1 for a clip the asset does not contain, and
        // C++ '%' keeps the sign — indexing on that raw would read out of
        // bounds. Wrap the same way AssetManager::getRootMotion does so the
        // pose and its root-motion track stay describing the same clip.
        int wrapped = animIndex % animCount;
        if (wrapped < 0) wrapped += animCount;

        const ModelAnimation &anim = anims[wrapped];
        UpdateModelAnimation(model, anim, frame);

        // 3. Neutralize root motion in the pose.
        //
        //    raylib's glTF loader runs BuildPoseFromParentJoints at load, so
        //    keyframePoses are already in model space — zeroing the root bone
        //    would mean subtracting from all 66 bones. Offsetting the draw
        //    position is equivalent and costs one vector subtract.
        //
        //    This only pins the mesh to the capsule. The travel itself is
        //    consumed by gameplay (Player::applyRootMotion), which is what
        //    keeps the two in agreement.
        const RootMotion::Track &track =
            assets.getRootMotion(renderData.assetId, animIndex);
        if (track.hasMotion) {
            Vector3 offset = RootMotion::sampleOffset(track, frame);
            // Scale componentwise: DrawModelEx below applies the full scale
            // vector, so using scale.x alone mis-cancels a non-uniform scale
            // (reachable via Enemy::visual_size).
            offset = Vector3Multiply(offset, renderData.transform.scale);
            offset = RootMotion::toWorld(offset, renderData.transform.rotation.y);
            drawPosition = Vector3Subtract(drawPosition, offset);
        }
    }

    // 4. Draw.
    // The GLB was exported from Blender (Z-up). The Blender->glTF exporter
    // already pre-corrects the coordinate system inside the mesh, so a single
    // Y-axis rotation via DrawModelEx is sufficient.
    const Vector3 rotationAxis = {0.0f, 1.0f, 0.0f};
    DrawModelEx(model, drawPosition, rotationAxis,
                renderData.transform.rotation.y,
                renderData.transform.scale, WHITE);
}
