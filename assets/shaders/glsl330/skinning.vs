#version 330

#define MAX_BONE_NUM 128

// Input vertex attributes
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
in vec3 vertexNormal;
in vec4 vertexBoneIndices;
in vec4 vertexBoneWeights;

// Input uniform values
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
uniform mat4 boneMatrices[MAX_BONE_NUM];

// Output vertex attributes (to fragment shader)
out vec3 fragPosition;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;
out vec3 fragLocalPos;

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

    vec4 skinnedNormal =
        vertexBoneWeights.x*(boneMatrices[boneIndex0]*vec4(vertexNormal, 0.0)) +
        vertexBoneWeights.y*(boneMatrices[boneIndex1]*vec4(vertexNormal, 0.0)) +
        vertexBoneWeights.z*(boneMatrices[boneIndex2]*vec4(vertexNormal, 0.0)) +
        vertexBoneWeights.w*(boneMatrices[boneIndex3]*vec4(vertexNormal, 0.0));
    skinnedNormal.w = 0.0;

    // World-space position of the *posed* vertex, for the shadow lookup in the
    // fragment stage. It has to be built from skinnedPosition, not
    // vertexPosition: the position VBO still holds the bind pose, so using it
    // would sample the shadow map wherever the T-pose happens to be standing.
    // raylib supplies matModel from DrawMesh, and mvp below is that same matrix
    // times view*projection.
    fragPosition = vec3(matModel*skinnedPosition);
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragNormal = normalize(vec3(matNormal*skinnedNormal));
    fragLocalPos = skinnedPosition.xyz;

    gl_Position = mvp*skinnedPosition;
}