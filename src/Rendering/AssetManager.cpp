#include <Rendering/AssetManager.h>
#include <Core/AssetPaths.h>
#include <Rendering/ShaderLibrary.h>
#include <iostream>
#include <raymath.h>
#include <utility>

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
  // ShaderLibrary, not raylib's LoadShader: skinning.fs pulls in
  // shadow_common.glsl, and the driver has no #include of its own.
  skinningShader = ShaderLibrary::load(assets::path("shaders/glsl330/skinning.vs"),
                                       assets::path("shaders/glsl330/skinning.fs"));
  skinnedDepthShader =
      ShaderLibrary::load(assets::path("shaders/glsl330/skinning_depth.vs"),
                          assets::path("shaders/glsl330/depth.fs"));
  skinningShaderLoaded = true; // guard the load attempt even if compile fails
  if (skinningShader.id == 0) {
    TraceLog(LOG_ERROR,
             "AssetManager: failed to load GPU skinning shader; skinned "
             "models will fall back to CPU skinning.");
  }
  if (skinnedDepthShader.id == 0) {
    TraceLog(LOG_ERROR, "AssetManager: failed to load skinned depth shader; "
                        "characters will not cast shadows.");
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

  // Extract every clip's root motion up front. Gameplay needs these to drive
  // the controller and the renderer needs them to pin the mesh to the capsule,
  // and both would otherwise rescan all keyframes every frame.
  data.rootTracks.reserve(static_cast<size_t>(animCount));
  for (int i = 0; i < animCount; i++) {
    data.rootTracks.push_back(RootMotion::buildTrack(anims[i]));
    const RootMotion::Track &track = data.rootTracks.back();
    TraceLog(LOG_INFO,
             "AssetManager: clip %d '%s' duration=%.2fs travel=%.3f %s "
             "speed=%.2f u/s",
             i, anims[i].name, track.duration,
             Vector3Length(track.totalDelta),
             track.hasMotion ? "root-motion" : "in-place", track.authoredSpeed);
  }

  animations[id] = std::move(data);
}

void AssetManager::loadSound(AssetID id, const std::string &filePath) {
  // Silently idempotent, unlike loadModel's warn-and-skip.
  //
  // GameplayState's constructor loads its six SFX unconditionally, and that
  // constructor now runs on every phase change and on every F5 reload -- so
  // warning here would print six lines each time, into the same console the F4
  // spawn readout is written to. Re-requesting a sound that is already resident
  // is the expected case for this call, not a mistake worth reporting.
  if (sounds.find(id) != sounds.end()) return;
  Sound sound = LoadSound(filePath.c_str());
  // raylib returns a zeroed Sound for a file it could not open, and
  // SoundController::playSFX skips those without a word -- so a missing or
  // misnamed SFX file is otherwise completely silent in both senses. Say so
  // once, at load, where the path is still in hand.
  if (sound.stream.buffer == nullptr) {
    TraceLog(LOG_WARNING, "AssetManager: sound '%s' failed to load; it will be silent.",
             filePath.c_str());
  }
  sounds[id] = sound;
}

void AssetManager::loadMusic(AssetID id, const std::string &filePath) {
  if (musics.find(id) != musics.end()) {
    std::cerr << "AssetManager Warning: Music already loaded for this AssetID.\n";
    return;
  }
  Music music = LoadMusicStream(filePath.c_str());
  musics[id] = music;
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

Sound AssetManager::getSound(AssetID id) const {
  auto it = sounds.find(id);
  if (it != sounds.end()) {
    return it->second;
  }
  static Sound kNullSound{};
  TraceLog(LOG_ERROR, "AssetManager: no sound loaded for AssetID %d", static_cast<int>(id));
  return kNullSound;
}

Music AssetManager::getMusic(AssetID id) const {
  auto it = musics.find(id);
  if (it != musics.end()) {
    return it->second;
  }
  static Music kNullMusic{};
  TraceLog(LOG_ERROR, "AssetManager: no music loaded for AssetID %d", static_cast<int>(id));
  return kNullMusic;
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

int AssetManager::findAnimation(AssetID id, const std::string &name) const {
  auto aliasIt = animAliases.find(id);
  AssetID resolvedId = (aliasIt != animAliases.end()) ? aliasIt->second : id;

  auto it = animations.find(resolvedId);
  if (it == animations.end() || it->second.anims == nullptr) return -1;

  for (int i = 0; i < it->second.count; i++) {
    if (name == it->second.anims[i].name) return i;
  }

  TraceLog(LOG_WARNING, "AssetManager: no clip named '%s' for AssetID %d",
           name.c_str(), static_cast<int>(id));
  return -1;
}

const RootMotion::Track &AssetManager::getRootMotion(AssetID id,
                                                     int animIndex) const {
  // Empty track shared by every miss. hasMotion is false, so callers fall
  // through to their no-root-motion path instead of needing a null check.
  static const RootMotion::Track kNullTrack{};

  auto aliasIt = animAliases.find(id);
  AssetID resolvedId = (aliasIt != animAliases.end()) ? aliasIt->second : id;

  auto it = animations.find(resolvedId);
  if (it == animations.end()) return kNullTrack;

  const auto &tracks = it->second.rootTracks;
  if (tracks.empty()) return kNullTrack;

  // Match SkinnedEntityRenderer's animIndex % animCount so the track always
  // describes the clip actually being played.
  int wrapped = animIndex % static_cast<int>(tracks.size());
  if (wrapped < 0) wrapped += static_cast<int>(tracks.size());
  return tracks[static_cast<size_t>(wrapped)];
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

  for (auto &pair : sounds) {
    UnloadSound(pair.second);
  }
  sounds.clear();

  for (auto &pair : musics) {
    UnloadMusicStream(pair.second);
  }
  musics.clear();

  // We own the skinning shaders (UnloadModel never unloads material shaders),
  // so unload them exactly once here.
  if (skinningShaderLoaded) {
    if (skinningShader.id != 0) UnloadShader(skinningShader);
    if (skinnedDepthShader.id != 0) UnloadShader(skinnedDepthShader);
  }
  skinningShaderLoaded = false;
  skinningShader = Shader{};
  skinnedDepthShader = Shader{};
}
