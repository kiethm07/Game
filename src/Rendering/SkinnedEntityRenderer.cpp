#include <Rendering/SkinnedEntityRenderer.h>
#include <Rendering/AnimUtils.h>

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
        const ModelAnimation &anim = anims[animIndex % animCount];
        UpdateModelAnimation(model, anim, frame);

        // 3. Cancel root motion (self-guards the frame index).
        drawPosition = AnimUtils::cancelRootMotion(
            anim, frame,
            renderData.transform.position,
            renderData.transform.rotation.y,
            renderData.transform.scale.x);
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
