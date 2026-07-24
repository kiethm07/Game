#include <Rendering/AnimUtils.h>
#include <Rendering/AssetManager.h>
#include <iostream>

AssetManager::~AssetManager() { unloadAll(); }

void AssetManager::loadModel(AssetID id, const std::string &filePath) {
  if (models.find(id) != models.end()) {
    std::cerr
        << "AssetManager Warning: Model already loaded for this AssetID.\n";
    return;
  }
  Model model = LoadModel(filePath.c_str());

  // GLB files (e.g. from Mixamo) may lack embedded textures, leaving the
  // material albedo map empty. Without a texture, raylib renders the mesh
  // using the material's diffuse color — which defaults to {0,0,0,0}
  // (transparent), making the model invisible.  Assign a visible fallback.
  for (int i = 0; i < model.materialCount; i++) {
    Texture2D albedo = model.materials[i].maps[MATERIAL_MAP_DIFFUSE].texture;
    if (albedo.id == 0) {
      // No texture — set a neutral gray so the mesh is always visible.
      model.materials[i].maps[MATERIAL_MAP_DIFFUSE].color = GRAY;
    }
  }

  // Route skinned meshes through the GPU skinning shader.
  setupGpuSkinning(model);

  models[id] = model;
}

void AssetManager::ensureSkinningShader() {
  if (skinningShaderLoaded) return;
  // Desktop raylib uses OpenGL 3.3 → GLSL 330.
  skinningShader = LoadShader(ASSET_DIR "/shaders/glsl330/skinning.vs",
                              ASSET_DIR "/shaders/glsl330/skinning.fs");
  skinningShaderLoaded = true; // guard the load attempt even if compile fails
  if (skinningShader.id == 0) {
    TraceLog(LOG_ERROR,
             "AssetManager: failed to load GPU skinning shader; skinned "
             "models will fall back to CPU skinning.");
  }
}

void AssetManager::setupGpuSkinning(Model &model) {
  bool hasSkin = false;
  for (int m = 0; m < model.meshCount; m++) {
    if (model.meshes[m].boneCount > 0 &&
        model.meshes[m].boneIndices != nullptr) {
      hasSkin = true;
      break;
    }
  }
  if (!hasSkin) return;

  ensureSkinningShader();
  if (skinningShader.id == 0) return; // shader unavailable — keep CPU skinning

  // With SUPPORT_GPU_SKINNING enabled (see CMakeLists), raylib uploads the
  // per-vertex bone index/weight VBOs and the position VBO holds the bind pose.
  // Attaching the skinning shader is all that's left; UpdateModelAnimation
  // refreshes the bone-matrix uniform each frame at draw time.
  for (int i = 0; i < model.materialCount; i++) {
    model.materials[i].shader = skinningShader;
  }
}

void AssetManager::loadAnimations(AssetID id, const std::string &filePath) {
  if (animations.find(id) != animations.end()) {
    std::cerr << "AssetManager Warning: Animations already loaded for this "
                 "AssetID.\n";
    return;
  }
  int animCount = 0;
  ModelAnimation *anims = LoadModelAnimations(filePath.c_str(), &animCount);

  AnimationData data;
  data.anims = anims;
  data.count = animCount;
  animations[id] = data;
}

void AssetManager::shareModel(AssetID alias, AssetID source) {
  if (models.find(source) == models.end()) {
    std::cerr << "AssetManager Error: shareModel() called before the source "
                 "AssetID has been loaded. Call loadModel(source) first.\n";
    return;
  }
  modelAliases[alias] = source;
}

Model &AssetManager::getModel(AssetID id) {
  // Resolve alias transparently.
  auto aliasIt = modelAliases.find(id);
  AssetID resolvedId = (aliasIt != modelAliases.end()) ? aliasIt->second : id;

  auto it = models.find(resolvedId);
  if (it != models.end()) {
    return it->second;
  }

  // Miss: return a shared zero-initialized sentinel. It is NEVER inserted into
  // `models`, so unloadAll() can never feed this garbage Model to UnloadModel.
  // DrawModelEx on a zero Model (meshCount == 0) is a harmless no-op.
  static Model kNullModel{};
  TraceLog(LOG_ERROR, "AssetManager: no model loaded for AssetID %d",
           static_cast<int>(id));
  return kNullModel;
}

void AssetManager::shareAnimations(AssetID alias, AssetID source) {
  if (animations.find(source) == animations.end()) {
    std::cerr
        << "AssetManager Error: shareAnimations() called before the source "
           "AssetID has been loaded. Call loadAnimations(source) first.\n";
    return;
  }
  animAliases[alias] = source;
}

ModelAnimation *AssetManager::getAnimations(AssetID id, int &outAnimCount) {
  // Resolve alias transparently.
  auto aliasIt = animAliases.find(id);
  AssetID resolvedId = (aliasIt != animAliases.end()) ? aliasIt->second : id;

  auto it = animations.find(resolvedId);
  if (it != animations.end()) {
    outAnimCount = it->second.count;
    return it->second.anims;
  }
  outAnimCount = 0;
  return nullptr;
}

void AssetManager::unloadAll() {
  for (auto &pair : models) {
    UnloadModel(pair.second);
  }
  models.clear();

  for (auto &pair : animations) {
    if (pair.second.anims != nullptr) {
      UnloadModelAnimations(pair.second.anims, pair.second.count);
    }
  }
  animations.clear();

  // We own the skinning shader (UnloadModel never unloads material shaders),
  // so unload it exactly once here.
  if (skinningShaderLoaded && skinningShader.id != 0) {
    UnloadShader(skinningShader);
  }
  skinningShaderLoaded = false;
  skinningShader = Shader{};
}
