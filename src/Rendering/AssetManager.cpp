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
  models[id] = model;
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

Model &AssetManager::getModel(AssetID id) {
  auto it = models.find(id);
  if (it != models.end()) {
    return it->second;
  }

  std::cerr << "AssetManager Error: Model not found for requested AssetID!\n";
  // Fallback: If they request a missing model, this will insert a
  // zero-initialized one and return it to prevent a hard C++ crash, though
  // raylib might just draw nothing.
  return models[id];
}

ModelAnimation *AssetManager::getAnimations(AssetID id, int &outAnimCount) {
  auto it = animations.find(id);
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
}
