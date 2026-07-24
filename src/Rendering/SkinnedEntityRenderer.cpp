#include <Rendering/SkinnedEntityRenderer.h>
#include <Rendering/AnimUtils.h>

void SkinnedEntityRenderer::draw(AssetManager &assets,
                                 const CharacterRenderData &renderData) {
    // 1. Fetch model and animations from the asset manager.
    Model &model   = assets.getModel(renderData.assetId);
    int animCount  = 0;
    ModelAnimation *anims = assets.getAnimations(renderData.assetId, animCount);

    const int animIndex = renderData.animation.animIndex;
    const int animFrame = renderData.animation.animFrame;

    // 2. Drive the skeleton to the current frame.
    AnimUtils::applyAnimationFrame(model, anims, animCount, animIndex, animFrame);

    // 3. Compute draw position with root motion cancelled.
    Vector3 drawPosition = renderData.transform.position;
    if (anims != nullptr && animCount > 0) {
        int safeAnimIndex    = animIndex % animCount;
        int keyframeCount    = anims[safeAnimIndex].keyframeCount;
        if (keyframeCount > 0) {
            int safeFrame = animFrame % keyframeCount;
            drawPosition  = AnimUtils::cancelRootMotion(
                anims[safeAnimIndex], safeFrame,
                renderData.transform.position,
                renderData.transform.rotation.y,
                renderData.transform.scale.x);
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
