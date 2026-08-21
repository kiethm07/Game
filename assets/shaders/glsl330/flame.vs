#version 330

// The campfire flame. Position only.
//
// No normal, no texture coordinate and no vertex colour, because none of them
// are read: the flame is unlit by design (see flame.fs) and its four shells
// carry flat baseColorFactors rather than maps. Declaring attributes a shader
// does not use costs a vertex buffer binding per draw for nothing.
in vec3 vertexPosition;

uniform mat4 mvp;

void main()
{
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
