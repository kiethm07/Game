#version 330

// Textured, lit, shadow-receiving shading for the level mesh.
//
// The lighting is world.fs's, not skinning.fs's: this is architecture and
// terrain sharing a frame with the obstacles, so it has to agree with them
// about how bright a lit surface is and how dark a shadow reads. The one thing
// it adds is the albedo sample -- world.fs has no texture at all, which is why
// it could not simply be reused.

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;
in vec4 fragColor;

out vec4 finalColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

// The lighting constants are in mood_common.glsl, shared with world.fs and
// grass.fs. If these three ever disagree, a wall in the level mesh and a
// collision proxy standing against it shade differently, and the greybox stops
// being a usable reference for the real art -- which is why they are no longer
// three separate copies.

#include "shadow_common.glsl"
#include "mood_common.glsl"

void main()
{
    // A material with no albedo map gets raylib's default 1x1 white texture
    // bound by GameRenderer, so this sample is white rather than undefined and
    // the material's baseColorFactor (colDiffuse) carries the colour on its
    // own. That is the greybox's normal case.
    vec4 texelColor = texture(texture0, fragTexCoord);

    vec3 normal = normalize(fragNormal);
    vec3 lightVec = normalize(-lightDir);

    float key = max(dot(normal, lightVec), 0.0);
    float shadow = shadowFactor(fragPosition, normal, lightVec);

    vec3 light = MOOD_AMBIENT_TINT*MOOD_AMBIENT*mix(1.0, MOOD_SHADOW_AMBIENT_SCALE, shadow)
               + MOOD_KEY_TINT*MOOD_KEY*key*(1.0 - shadow);

    vec4 lit = vec4(texelColor.rgb*light, texelColor.a)*colDiffuse*fragColor;

    // Fogged after colDiffuse and the vertex colour, not before: fog is what
    // the air between the camera and this surface does to it, so everything
    // that decides the surface's own colour has to have happened first.
    finalColor = vec4(applyFog(lit.rgb, fragPosition), lit.a);
}
