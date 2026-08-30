#version 330

in vec3 fragPosition;
in vec3 fragNormal;
in vec4 fragColor;

out vec4 finalColor;

// Obstacle geometry is flat authored colours (PhysicsObstacle::color), not
// textures, so it does not need the character shader's rescue constants -- its
// ambient is far higher to lift dark steel albedo, which here would only wash
// the colours out.
//
// The three values themselves used to sit here, copied into level.fs and
// grass.fs. They live in mood_common.glsl now, so the level mesh, the grass and
// any obstacle standing against them cannot drift apart.

#include "shadow_common.glsl"
#include "mood_common.glsl"

void main()
{
    vec3 normal = normalize(fragNormal);
    vec3 lightVec = normalize(-lightDir);

    float key = max(dot(normal, lightVec), 0.0);
    float shadow = shadowFactor(fragPosition, normal, lightVec);

    // Shadow kills the key term outright and dims the ambient floor. Dimming
    // ambient too is what keeps a shadow readable on a face that was already
    // turned away from the light.
    vec3 light = MOOD_AMBIENT_TINT*MOOD_AMBIENT*mix(1.0, MOOD_SHADOW_AMBIENT_SCALE, shadow)
               + MOOD_KEY_TINT*MOOD_KEY*key*(1.0 - shadow);

    finalColor = vec4(applyFog(fragColor.rgb*light, fragPosition), fragColor.a);
}
