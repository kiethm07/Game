#include <Rendering/AnimUtils.h>
#include <Rendering/RootMotion.h>
#include <Rendering/ScopedMaterialShader.h>
#include <Rendering/SkinnedEntityRenderer.h>
#include <raymath.h>
#include <vector>

void SkinnedEntityRenderer::draw(AssetManager &assets,
                                 const CharacterRenderData &renderData,
                                 RenderPass pass) {
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
        auto wrap = [animCount](int index) {
            int wrapped = index % animCount;
            if (wrapped < 0) wrapped += animCount;
            return wrapped;
        };

        const int   fromIndex = renderData.animation.blendFromIndex;
        const float fromFrame =
            renderData.animation.blendFromTime * AnimUtils::ANIM_SAMPLE_RATE;
        const float blend = renderData.animation.blend;
        const bool  blending = (fromIndex >= 0) && (blend < 1.0f);

        if (blending) {
            // raylib 6.0 does the per-bone work: lerp on translation/scale,
            // slerp on rotation, across the two clips. blend == 0 is entirely
            // the outgoing clip, 1 entirely the incoming one.
            UpdateModelAnimationEx(model, anims[wrap(fromIndex)], fromFrame,
                                   anims[wrap(animIndex)], frame, blend);
        } else {
            UpdateModelAnimation(model, anims[wrap(animIndex)], frame);
        }

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
        auto rootOffset = [&assets, &renderData](int index, float atFrame) {
            const RootMotion::Track &track =
                assets.getRootMotion(renderData.assetId, index);
            // Zero for in-place clips, so a fade between an in-place clip and a
            // travelling one eases the cancellation in exactly as the pose
            // eases in.
            if (!track.hasMotion) return Vector3{0.0f, 0.0f, 0.0f};
            return RootMotion::sampleOffset(track, atFrame);
        };

        Vector3 offset = rootOffset(animIndex, frame);
        if (blending) {
            // The blended pose carries a blended root translation — the root
            // bone is interpolated like every other one. Cancelling only the
            // incoming clip's offset would slide the mesh off the capsule for
            // the length of every fade touching a root-motion clip, so the
            // cancellation has to be interpolated with the same weight.
            offset = Vector3Lerp(rootOffset(fromIndex, fromFrame), offset, blend);
        }

        // Scale componentwise: DrawModelEx below applies the full scale
        // vector, so using scale.x alone mis-cancels a non-uniform scale
        // (reachable via Enemy::visual_size).
        offset = Vector3Multiply(offset, renderData.transform.scale);
        offset = RootMotion::toWorld(offset, renderData.transform.rotation.y);
        drawPosition = Vector3Subtract(drawPosition, offset);
    }

    // 4. Set custom shader uniforms (e.g. dissolve)
    for (int i = 0; i < model.materialCount; i++) {
        Shader shader = model.materials[i].shader;
        int dissolveLoc = GetShaderLocation(shader, "dissolveAmount");
        if (dissolveLoc >= 0) {
            SetShaderValue(shader, dissolveLoc, &renderData.dissolveProgress, SHADER_UNIFORM_FLOAT);
        }
        int typeLoc = GetShaderLocation(shader, "decayType");
        if (typeLoc >= 0) {
            int decayVal = static_cast<int>(renderData.decayType);
            SetShaderValue(shader, typeLoc, &decayVal, SHADER_UNIFORM_INT);
        }
    }

    // 5. Draw.
    // The GLB was exported from Blender (Z-up). The Blender->glTF exporter
    // already pre-corrects the coordinate system inside the mesh, so a single
    // Y-axis rotation via DrawModelEx is sufficient.
    //
    // Everything above this point is deliberately shared by both passes. The
    // shadow is only pose-accurate because the depth draw samples the very same
    // clip, blend weight and root-motion cancellation the scene draw does —
    // re-deriving any of it here would show up as a shadow that lags or
    // sidesteps the character. Posing twice per frame is cheap: with GPU
    // skinning, UpdateModelAnimation only refreshes 66 bone matrices and does
    // no vertex work.
    const Vector3 rotationAxis = {0.0f, 1.0f, 0.0f};

    if (pass == RenderPass::ShadowDepth) {
        Shader depthShader = assets.getSkinnedDepthShader();
        if (depthShader.id == 0) return; // shader unavailable: cast nothing

        ScopedMaterialShader swap(model, depthShader);
        DrawModelEx(model, drawPosition, rotationAxis,
                    renderData.transform.rotation.y,
                    renderData.transform.scale, WHITE);
        return;
    }

    DrawModelEx(model, drawPosition, rotationAxis,
                renderData.transform.rotation.y,
                renderData.transform.scale, WHITE);
}
