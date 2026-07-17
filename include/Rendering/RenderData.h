#pragma once

#include <raylib.h>
#include <Rendering/AssetID.h>

struct CharacterRenderData {
    AssetID assetId;
    int currentAnimationIndex;
    int currentAnimationFrame;
    Vector3 position;
    Vector3 rotation;
    Vector3 scale;
};
