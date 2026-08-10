#pragma once

#include <raylib.h>
#include <Rendering/AssetID.h>

enum class DecayType {
    ASH_DECAY = 0,
    PETAL_DECAY = 1
};

/// World-space transform for an entity.
struct TransformData {
    Vector3 position = {0.0f, 0.0f, 0.0f};
    Vector3 rotation = {0.0f, 0.0f, 0.0f}; ///< Euler angles in degrees
    Vector3 scale    = {1.0f, 1.0f, 1.0f};
};

/// Current skeletal animation playback state.
struct AnimationState {
    int   animIndex = 0;   ///< Index into the ModelAnimation array
    float animTime  = 0.0f; ///< Elapsed playback time in seconds (framerate-independent)

    /// Clip being cross-faded out of, or -1 for none. The defaults here mean
    /// "not blending", so an entity that never sets them draws from animIndex
    /// alone exactly as before.
    int   blendFromIndex = -1;
    float blendFromTime  = 0.0f;

    /// Weight of animIndex against blendFromIndex, in raylib's convention:
    /// 0 is entirely the outgoing clip, 1 entirely the current one.
    float blend = 1.0f;
};

/// All data the renderer needs to draw one entity per frame.
struct CharacterRenderData {
    AssetID       assetId;
    TransformData transform;
    AnimationState animation;
    float dissolveProgress = 0.0f;
    DecayType decayType = DecayType::ASH_DECAY;
};
