#pragma once

#include <raylib.h>
#include <Rendering/AssetID.h>

/// World-space transform for an entity.
struct TransformData {
    Vector3 position = {0.0f, 0.0f, 0.0f};
    Vector3 rotation = {0.0f, 0.0f, 0.0f}; ///< Euler angles in degrees
    Vector3 scale    = {1.0f, 1.0f, 1.0f};
};

/// Current skeletal animation playback state.
struct AnimationState {
    int animIndex = 0; ///< Index into the ModelAnimation array
    int animFrame = 0; ///< Current keyframe number
};

/// All data the renderer needs to draw one entity per frame.
struct CharacterRenderData {
    AssetID       assetId;
    TransformData transform;
    AnimationState animation;
};
