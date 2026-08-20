#version 330

// Lit, shadow-receiving shading for the grass. Casts nothing -- GameRenderer
// simply never hands the detail model to the depth pass -- but it receives, so
// a tree's shadow falls across the meadow the way it falls across the ground.

in vec3 fragPosition;
in vec4 fragColor;
in float fragViewDepth;

out vec4 finalColor;

uniform vec4 colDiffuse;

// Copied verbatim from level.fs, and they have to stay copied. The whole point
// of this shader is that a lit blade tip on flat ground comes out the same
// colour as the terrain pixel beside it: same baseColorFactor, same lighting
// arithmetic. Let these three drift and the grass separates from the ground
// again, which is the fault this replaced a photographic grass texture to fix.
const float AMBIENT = 0.60;
const float KEY = 0.55;
const float SHADOW_AMBIENT_SCALE = 0.62;

// Blades are a few centimetres wide, so past ~30 m they are thinner than a
// pixel and crawl as the camera moves. Fading their vertex shading out to 1.0
// leaves distant grass at exactly the terrain colour, so there is nothing left
// for the aliasing to flicker against -- and it hides GameRenderer's distance
// cull, which sits beyond FADE_END.
const float FADE_START = 30.0;
const float FADE_END = 70.0;

#include "shadow_common.glsl"

void main()
{
    // Constant, not the blade plane's, and NOT flipped on gl_FrontFacing.
    //
    // Grass is drawn with backface culling off, because a blade is a
    // single-sided triangle and its far face still has to be on screen. That is
    // a visibility question. Lighting is a separate one, and it is already
    // settled here: with an up normal both faces of a blade shade identically,
    // and identically to the ground. Flipping this to (0,-1,0) on back faces
    // would send half of every blade to `key = 0` -- ambient only -- and the
    // meadow would come out mottled dark.
    vec3 normal = vec3(0.0, 1.0, 0.0);
    vec3 lightVec = normalize(-lightDir);

    float key = max(dot(normal, lightVec), 0.0);
    float shadow = shadowFactor(fragPosition, normal, lightVec);

    float light = AMBIENT*mix(1.0, SHADOW_AMBIENT_SCALE, shadow) + KEY*key*(1.0 - shadow);

    // fragColor is downward modulation only -- dark at the blade's base for
    // contact shading, 1.0 at the tip -- never the colour itself. The colour is
    // colDiffuse, straight off the terrain material.
    float fade = 1.0 - smoothstep(FADE_START, FADE_END, fragViewDepth);
    vec3 shade = mix(vec3(1.0), fragColor.rgb, fade);

    finalColor = vec4(colDiffuse.rgb*shade*light, 1.0);
}
