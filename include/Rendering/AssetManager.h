#pragma once

#include <raylib.h>
#include <unordered_map>
#include <string>
#include <vector>
#include <Rendering/AssetID.h>
#include <Rendering/RootMotion.h>

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

    /// Root motion track for one clip, extracted once at load time.
    /// Out-of-range indices and unloaded assets return an empty track
    /// (hasMotion == false), so callers never need to bounds-check.
    const RootMotion::Track& getRootMotion(AssetID id, int animIndex) const;

    /// The shader skinned models are drawn with. Handed out so ShadowMap can
    /// push its lightVP/shadowMap uniforms into it once per frame — by
    /// reference, because those uniforms have to land on the very Shader the
    /// materials hold, not a copy.
    Shader& getSkinningShader() { return skinningShader; }

    /// Depth-only counterpart of the skinning shader, for the shadow pass.
    /// Assign it onto a model's materials (not BeginShaderMode) so raylib still
    /// uploads the bone matrices — see SkinnedEntityRenderer.
    Shader getSkinnedDepthShader() const { return skinnedDepthShader; }

    /// Index of the clip named `name`, or -1 if the asset has no such clip.
    ///
    /// Prefer this over hardcoded indices: clip order is an artifact of the
    /// export toolchain, not something the asset guarantees. The Blender root
    /// motion bake reorders clips alphabetically, which silently remapped every
    /// hardcoded index the first time it ran.
    int findAnimation(AssetID id, const std::string& name) const;

    // --- Cleanup ---
    void unloadAll();

private:
    // Shared GPU skinning shader, lazily loaded and attached to skinned models'
    // materials so animation runs on the GPU (bone-matrix uniform) instead of
    // re-skinning + re-uploading the whole mesh on the CPU each frame.
    Shader skinningShader{};

    // Same vertex format, position only: what the shadow depth pass swaps onto
    // the materials. Loaded and unloaded alongside skinningShader because the
    // two only make sense as a pair — a pose skinned differently between them
    // would be a shadow that disagrees with the model.
    Shader skinnedDepthShader{};

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
        /// One track per clip, parallel to anims. Built in loadAnimations so
        /// the keyframe scan happens once per clip rather than once per entity
        /// per frame.
        std::vector<RootMotion::Track> rootTracks;
    };
    std::unordered_map<AssetID, AnimationData> animations;
};

