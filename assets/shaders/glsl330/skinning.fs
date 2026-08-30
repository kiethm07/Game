#version 330

// Input vertex attributes (from vertex shader)
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;
in vec3 fragLocalPos;

// Output fragment color
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform float dissolveAmount; // 0.0 (fully visible) to 1.0 (fully dissolved)
uniform int decayType; // 0 = Ash, 1 = Petal

// Simple hash-based procedural noise
float hash(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}
float noise(vec3 x) {
    vec3 i = floor(x);
    vec3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash(i + vec3(0,0,0)), hash(i + vec3(1,0,0)), f.x),
                   mix(hash(i + vec3(0,1,0)), hash(i + vec3(1,1,0)), f.x), f.y),
               mix(mix(hash(i + vec3(0,0,1)), hash(i + vec3(1,0,1)), f.x),
                   mix(hash(i + vec3(0,1,1)), hash(i + vec3(1,1,1)), f.x), f.y), f.z);
}

// The albedo maps on this armour set are dark steel, and sampling them straight
// reads flat and murky, so the character rig runs a higher ambient and a
// midtone lift than the environment does. Both live in mood_common.glsl as
// MOOD_CHAR_*, next to the environment values they have to stay in balance
// with: a character lit for a bright scene stands out as a cutout once the
// world around them goes overcast.
//
// The key direction used to be a constant here. It is a uniform (declared with
// the shadow block below) because the shadow pass has to render from that exact
// direction -- see ShadowMap, which owns the value and pushes it into this
// shader and into world.fs.

#include "shadow_common.glsl"
#include "mood_common.glsl"

void main()
{
    // The dissolve effect evaluates height and noise. 
    // We map height from ~0.0 (feet) to ~2.0 (head) roughly.
    // As dissolveAmount increases from 0 to 1, threshold drops from 2.5 to -0.5.
    if (dissolveAmount > 0.0) {
        float n = 0.0;
        if (decayType == 1) {
            // Petal decay: larger, rounder clumps by using abs(noise - 0.5) to form cell-like structures
            n = 1.0 - abs(noise(fragLocalPos * 10.0) * 2.0 - 1.0); 
        } else {
            // Ash decay: original high-frequency noise
            n = noise(fragLocalPos * 15.0);
        }
        
        // The effective "health" of the fragment:
        // top of the head is higher Y, so it gets eaten first.
        float dissolveThreshold = 2.5 - (dissolveAmount * 3.0);
        float pixelValue = fragLocalPos.y + (n * 0.5); 
        
        if (pixelValue > dissolveThreshold) {
            discard; // Burn away!
        }
        
        // Edge burn effect
        float edgeDist = dissolveThreshold - pixelValue;
        if (edgeDist < 0.1) {
            if (decayType == 1) {
                finalColor = vec4(1.0, 0.5, 0.8, 1.0); // Bright pink petal edge
            } else {
                finalColor = vec4(1.0, 0.4, 0.0, 1.0); // Bright orange ash ember
            }
            return;
        }
    }

    vec4 texelColor = texture(texture0, fragTexCoord);

    vec3 normal = normalize(fragNormal);
    vec3 lightVec = normalize(-lightDir);

    float key = max(dot(normal, lightVec), 0.0);
    float shadow = shadowFactor(fragPosition, normal, lightVec);

    vec3 light = MOOD_AMBIENT_TINT*MOOD_CHAR_AMBIENT*mix(1.0, MOOD_CHAR_SHADOW_AMBIENT_SCALE, shadow)
               + MOOD_KEY_TINT*MOOD_CHAR_KEY*key*(1.0 - shadow);

    vec3 shaded = pow(texelColor.rgb*light, vec3(MOOD_CHAR_LIFT));

    vec4 lit = vec4(shaded, texelColor.a)*colDiffuse*fragColor;

    // Fogged like everything else, so a distant enemy fades into the mist at
    // the same rate the ground under them does. The dissolve branches above
    // return early and stay unfogged on purpose: those are emissive embers, not
    // surfaces the air is in front of.
    finalColor = vec4(applyFog(lit.rgb, fragPosition), lit.a);
}
