#version 330

// Depth-only pass for skinned characters. This is skinning.vs stripped to the
// position pipeline, and it must stay in lockstep with it: the shadow is only
// pose-accurate because both shaders skin by the same weights against the same
// bone matrices.
//
// The uniform MUST stay named `boneMatrices`. raylib resolves
// SHADER_LOC_MATRIX_BONETRANSFORMS by that exact name (rcore.c) and uploads to
// it from the *material's* shader locs inside DrawMesh (rmodels.c). That is why
// the depth pass swaps this shader onto the model's materials rather than
// binding it with BeginShaderMode — BeginShaderMode would leave boneMatrices
// unset and every shadow would freeze in the bind pose.

#define MAX_BONE_NUM 128

in vec3 vertexPosition;
in vec4 vertexBoneIndices;
in vec4 vertexBoneWeights;

uniform mat4 mvp;
uniform mat4 boneMatrices[MAX_BONE_NUM];

void main()
{
    int boneIndex0 = int(vertexBoneIndices.x);
    int boneIndex1 = int(vertexBoneIndices.y);
    int boneIndex2 = int(vertexBoneIndices.z);
    int boneIndex3 = int(vertexBoneIndices.w);

    vec4 skinnedPosition =
        vertexBoneWeights.x*(boneMatrices[boneIndex0]*vec4(vertexPosition, 1.0)) +
        vertexBoneWeights.y*(boneMatrices[boneIndex1]*vec4(vertexPosition, 1.0)) +
        vertexBoneWeights.z*(boneMatrices[boneIndex2]*vec4(vertexPosition, 1.0)) +
        vertexBoneWeights.w*(boneMatrices[boneIndex3]*vec4(vertexPosition, 1.0));

    gl_Position = mvp*skinnedPosition;
}
