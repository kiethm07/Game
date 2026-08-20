#version 330

// The detail mesh -- grass -- drawn with DrawMesh from its own Model.
//
// This is level.vs with two attributes removed and one added, and each of the
// three differences is load-bearing rather than incidental.
//
// No vertexTexCoord: the blades carry no texture. Their colour is the terrain's
// own baseColorFactor, which is what makes a blade and the ground under it the
// same green (see grass.fs).
//
// No vertexNormal: the blades are lit from a constant up vector, not from their
// own geometry. A blade plane's normal is roughly horizontal, so lighting by it
// would shade grass like a wall -- bright edge-on to the sun, dark away from it
// -- and it could never match a ground surface whose normal points up. Dropping
// the attribute also saves 12 of the 32 bytes a detail vertex would cost, which
// over 148,000 blades is most of a megabyte.
//
// Plus vertexColor, which carries the base-to-tip shading. The name matters and
// is not a choice: raylib force-binds "vertexColor" to attribute location 3 at
// link time (rlgl.h, glBindAttribLocation), so a shader that spells it anything
// else reads location 3's generic default and every blade comes out black.
in vec3 vertexPosition;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;

out vec3 fragPosition;
out vec4 fragColor;
out float fragViewDepth;

void main()
{
    // World space, because that is what the shadow lookup samples with.
    fragPosition = vec3(matModel*vec4(vertexPosition, 1.0));
    fragColor    = vertexColor;

    gl_Position = mvp*vec4(vertexPosition, 1.0);

    // Distance from the camera, for the fade in grass.fs. Taken off the clip
    // position's w rather than from a viewPos uniform, because GameRenderer
    // does not upload one to any shader and adding one for this would be a
    // per-frame uniform to keep in step for no gain.
    fragViewDepth = gl_Position.w;
}
