#pragma once

#include <raylib.h>
#include <unordered_map>
#include <string>
#include <Rendering/AssetID.h>

class AssetManager {
public:
    AssetManager() = default;
    ~AssetManager();

    // Delete copy constructors to prevent accidental duplication
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    // --- Loading ---
    void loadModel(AssetID id, const std::string& filePath);
    void loadAnimations(AssetID id, const std::string& filePath);

    // --- Sharing ---
    // Register 'alias' as a reference to the model already loaded for 'source'.
    // No extra GPU allocation is made; both IDs return the same Model.
    // Only 'source' owns the model — do NOT call shareModel before loading source.
    void shareModel(AssetID alias, AssetID source);

    // Register 'alias' as a reference to the animations already loaded for 'source'.
    // Both IDs will return the same ModelAnimation array.
    // Only 'source' owns the animation data — do NOT call shareAnimations before loading source.
    void shareAnimations(AssetID alias, AssetID source);

    // --- Retrieval ---
    Model& getModel(AssetID id);
    ModelAnimation* getAnimations(AssetID id, int& outAnimCount);

    // --- Cleanup ---
    void unloadAll();

private:
    // Shared GPU skinning shader, lazily loaded and attached to skinned models'
    // materials so animation runs on the GPU (bone-matrix uniform) instead of
    // re-skinning + re-uploading the whole mesh on the CPU each frame.
    Shader skinningShader{};
    bool skinningShaderLoaded = false;
    void ensureSkinningShader();
    void setupGpuSkinning(Model &model);

    std::unordered_map<AssetID, Model> models;

    // Alias → owning AssetID. Resolved transparently in getModel().
    std::unordered_map<AssetID, AssetID> modelAliases;

    // Alias → owning AssetID. Resolved transparently in getAnimations().
    std::unordered_map<AssetID, AssetID> animAliases;

    struct AnimationData {
        ModelAnimation* anims = nullptr;
        int count = 0;
    };
    std::unordered_map<AssetID, AnimationData> animations;
};

