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
// Plus vertexColor, which carries the base-to-tip shading in rgb and the
// blade's height fraction in alpha. The name matters and is not a choice:
// raylib force-binds "vertexColor" to attribute location 3 at link time
// (rlgl.h, glBindAttribLocation), so a shader that spells it anything else
// reads location 3's generic default and every blade comes out black.
in vec3 vertexPosition;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;

// Seconds since the game started, from GameRenderer. Only the wind reads it.
//
// If GameRenderer ever stops uploading this it stays at GLSL's default 0.0 and
// the meadow simply stands still -- the failure mode is no wind, not broken
// grass.
uniform float time;

out vec3 fragPosition;
out vec4 fragColor;
out float fragViewDepth;

// --- Wind ------------------------------------------------------------------
//
// Travelling waves over world XZ rather than a per-blade oscillation: a gust
// has to cross the meadow to read as wind rather than as every blade twitching
// on its own clock. Two octaves, because a single sine is recognisably a sine
// once you watch it for a few seconds.
//
// Deliberately small. This is grass moving in still air, not a storm -- during
// a parry the meadow must not be the thing pulling the eye, and a metre-tall
// blade waving through 5 cm at the tip is at the edge of noticeable, which is
// where it belongs.
const vec2 WIND_DIR = vec2(0.8682, 0.4962);   // normalised
const float WIND_STRENGTH = 0.055;            // metres of travel at the tip
const float WIND_WAVELENGTH = 9.0;            // metres between gust crests
const float WIND_SPEED = 1.15;                // radians/second of the first octave

void main()
{
    // Object space is world space here: GameRenderer draws the detail meshes
    // with an identity transform and scatter_grass.py bakes world coordinates
    // into them, so displacing the vertex before the mvp multiply displaces it
    // in the world. Phase comes off the same coordinates for the same reason.
    float phase = dot(vertexPosition.xz, WIND_DIR)*(6.2831853/WIND_WAVELENGTH);
    float sway = sin(phase - time*WIND_SPEED)
               + 0.5*sin(phase*2.3 - time*WIND_SPEED*1.7);
    sway *= 0.6667;   // the two octaves sum to 1.5; back into -1..1

    // vertexColor.a is the vertex's height up the blade, written by
    // scatter_grass.py's shade_colour: 0 at the foot, one entry of RINGS at
    // each ring above it, 1 at the tip. Squared so the lower half of a blade
    // barely moves and the foot does not move at all -- a blade whose root
    // slides is a blade that has come out of the ground.
    float mask = vertexColor.a*vertexColor.a;
    vec3 drift = vec3(WIND_DIR.x, 0.0, WIND_DIR.y)*(sway*mask*WIND_STRENGTH);

    vec3 position = vertexPosition + drift;

    // World space, because that is what the shadow lookup samples with. Taken
    // after the drift so a blade's shading follows the blade.
    fragPosition = vec3(matModel*vec4(position, 1.0));
    fragColor    = vertexColor;

    gl_Position = mvp*vec4(position, 1.0);

    // Distance from the camera, for the fade in grass.fs. Taken off the clip
    // position's w rather than from a viewPos uniform, because GameRenderer
    // does not upload one to any shader and adding one for this would be a
    // per-frame uniform to keep in step for no gain.
    fragViewDepth = gl_Position.w;
}
