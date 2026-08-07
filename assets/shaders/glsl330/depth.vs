#version 330

// Depth-only pass for static geometry, rendered from the light's point of view.
// Position is the only attribute that matters: the framebuffer this writes into
// has no colour attachment, only a depth texture (see ShadowMap).
//
// This one shader serves both draw paths, because raylib sets `mvp` to match
// whichever is in use:
//   * rlgl immediate mode (PhysicsObstacle) -> mvp is view*projection, and the
//     vertices arrive already in world space (rlVertex3f applies the active
//     rlPushMatrix/rlMultMatrixf transform on the CPU).
//   * DrawMesh/DrawModel                    -> mvp is model*view*projection,
//     and the vertices arrive in local space.
// Either way, mvp*vertexPosition lands in light clip space.
in vec3 vertexPosition;

uniform mat4 mvp;

void main()
{
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
