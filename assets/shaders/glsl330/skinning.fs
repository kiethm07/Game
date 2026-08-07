#version 330

// Input vertex attributes (from vertex shader)
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

// Output fragment color
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

// The albedo maps on this armour set are dark steel, and sampling them straight
// reads flat and murky. AMBIENT is the floor every fragment gets, so nothing
// lands darker than the unlit look this replaced; KEY is what the lit side adds
// on top. Raise AMBIENT alone to brighten uniformly.
//
// The key direction used to be a constant here. It is a uniform (declared with
// the shadow block below) because the shadow pass has to render from that exact
// direction -- see ShadowMap, which owns the value and pushes it into this
// shader and into world.fs.
const float AMBIENT = 1.15;
const float KEY = 0.60;

// How far the ambient floor drops in shadow. AMBIENT above 1.0 means a fragment
// in shadow would otherwise still be brighter than the original unlit look, so
// without this a character standing in the pillar's shadow barely changes.
const float SHADOW_AMBIENT_SCALE = 0.72;

// Midtone lift, applied after the light term. A plain multiply bright enough to
// rescue the armour also clips the gold trim to a flat yellow; an exponent
// below 1 raises the middle of the range and leaves the highlights alone.
const float LIFT = 0.78;

#include "shadow_common.glsl"

void main()
{
    vec4 texelColor = texture(texture0, fragTexCoord);

    vec3 normal = normalize(fragNormal);
    vec3 lightVec = normalize(-lightDir);

    float key = max(dot(normal, lightVec), 0.0);
    float shadow = shadowFactor(fragPosition, normal, lightVec);

    float light = AMBIENT*mix(1.0, SHADOW_AMBIENT_SCALE, shadow) + KEY*key*(1.0 - shadow);

    vec3 shaded = pow(texelColor.rgb*light, vec3(LIFT));

    finalColor = vec4(shaded, texelColor.a)*colDiffuse*fragColor;
}
