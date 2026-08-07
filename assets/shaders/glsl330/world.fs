#version 330

in vec3 fragPosition;
in vec3 fragNormal;
in vec4 fragColor;

out vec4 finalColor;

// Obstacle geometry is flat authored colours (PhysicsObstacle::color), not
// textures, so it does not need skinning.fs's rescue constants -- AMBIENT there
// is above 1.0 to lift dark steel albedo, which here would only wash the
// colours out. These are picked instead so the lit and unlit faces of a box
// read apart, and so a shadow on the ground is unmistakable: lit ground lands
// near 1.06, shadowed ground near 0.37.
//
// level.fs carries these same three values, so the level mesh and any obstacle
// standing against it shade identically. Change them in both or they diverge.
const float AMBIENT = 0.60;
const float KEY = 0.55;
const float SHADOW_AMBIENT_SCALE = 0.62;

#include "shadow_common.glsl"

void main()
{
    vec3 normal = normalize(fragNormal);
    vec3 lightVec = normalize(-lightDir);

    float key = max(dot(normal, lightVec), 0.0);
    float shadow = shadowFactor(fragPosition, normal, lightVec);

    // Shadow kills the key term outright and dims the ambient floor. Dimming
    // ambient too is what keeps a shadow readable on a face that was already
    // turned away from the light.
    float light = AMBIENT*mix(1.0, SHADOW_AMBIENT_SCALE, shadow) + KEY*key*(1.0 - shadow);

    finalColor = vec4(fragColor.rgb*light, fragColor.a);
}
