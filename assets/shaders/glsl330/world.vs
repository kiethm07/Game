#version 330

// Static level geometry: the ground plane and every PhysicsObstacle. All of it
// is submitted through rlgl's immediate-mode batch (DrawPlane, DrawCube,
// DrawTriangle3D), which is what makes this shader look wrong at first glance.
//
// THE VERTICES ARE ALREADY IN WORLD SPACE. rlVertex3f applies the active
// rlPushMatrix/rlMultMatrixf transform on the CPU before it ever reaches the
// buffer, and rlNormal3f applies its rotation part to the normal. So this
// shader must NOT multiply by matModel the way raylib's stock shaders do — for
// the batch, matModel *is* that same transform, and applying it here would
// transform everything a second time. `mvp` for the batch is view*projection
// only, which is why gl_Position below is still correct.
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;

out vec3 fragPosition;
out vec3 fragNormal;
out vec4 fragColor;

void main()
{
    fragPosition = vertexPosition;
    fragNormal   = vertexNormal;
    fragColor    = vertexColor;

    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
