#version 330

// The level mesh, drawn with DrawModel.
//
// This exists separately from world.vs because the two get their vertices in
// different spaces. world.vs serves rlgl's immediate-mode batch, where
// rlVertex3f has already applied the active transform on the CPU, so
// multiplying by matModel there would transform everything twice. A Model goes
// through DrawMesh, which uploads matModel and leaves the vertices in object
// space -- so here the multiply is required, not forbidden.
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec3 fragNormal;
out vec4 fragColor;

void main()
{
    // World space, because that is what the shadow lookup samples with.
    fragPosition = vec3(matModel*vec4(vertexPosition, 1.0));
    fragTexCoord = vertexTexCoord;
    // matNormal is the inverse-transpose raylib uploads, so this stays correct
    // under a non-uniform scale rather than only under rigid transforms.
    fragNormal   = normalize(vec3(matNormal*vec4(vertexNormal, 1.0)));
    fragColor    = vertexColor;

    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
