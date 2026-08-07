#pragma once

#include <raylib.h>
#include <vector>

/// Swaps a shader onto every material of a model for the duration of a scope,
/// then puts the originals back.
///
/// BeginShaderMode does not reach models. rlgl's bound shader drives the
/// immediate-mode batch, but DrawMesh reads `material.shader` directly, so a
/// model drawn inside a BeginShaderMode scope quietly ignores it. Anything that
/// wants a Model drawn with a different shader — the depth pass, chiefly — has
/// to go through the materials, which is what this does.
///
/// For skinned models it is not merely a convenience: raylib uploads the bone
/// matrices from `material.shader.locs[SHADER_LOC_MATRIX_BONETRANSFORMS]`
/// inside DrawMesh. Bind the depth shader any other way and boneMatrices is
/// never set, so every shadow freezes in the bind pose while the models animate.
class ScopedMaterialShader {
public:
    ScopedMaterialShader(Model &model, Shader shader) : model(model) {
        saved.reserve(static_cast<size_t>(model.materialCount));
        for (int i = 0; i < model.materialCount; i++) {
            saved.push_back(model.materials[i].shader);
            model.materials[i].shader = shader;
        }
    }

    ~ScopedMaterialShader() {
        for (int i = 0; i < model.materialCount; i++) {
            model.materials[i].shader = saved[static_cast<size_t>(i)];
        }
    }

    ScopedMaterialShader(const ScopedMaterialShader &) = delete;
    ScopedMaterialShader &operator=(const ScopedMaterialShader &) = delete;

private:
    Model &model;
    std::vector<Shader> saved;
};
