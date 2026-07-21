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

  std::cerr << "AssetManager Error: Model not found for requested AssetID!\n";
  // Fallback: insert a zero-initialized model to avoid a hard crash.
  return models[resolvedId];
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
