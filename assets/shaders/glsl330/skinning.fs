#version 330

// Input vertex attributes (from vertex shader)
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

// Output fragment color
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

// The albedo maps on this armour set are dark steel, and sampling them straight
// reads flat and murky. There is no light rig in the scene, so shade against a
// fixed key direction instead. AMBIENT is the floor every fragment gets, so
// nothing lands darker than the unlit look this replaced; KEY is what the lit
// side adds on top. Raise AMBIENT alone to brighten uniformly.
const vec3 LIGHT_DIR = normalize(vec3(-0.35, -1.0, -0.55));
const float AMBIENT = 1.15;
const float KEY = 0.60;

// Midtone lift, applied after the light term. A plain multiply bright enough to
// rescue the armour also clips the gold trim to a flat yellow; an exponent
// below 1 raises the middle of the range and leaves the highlights alone.
const float LIFT = 0.78;

void main()
{
    vec4 texelColor = texture(texture0, fragTexCoord);

    vec3 normal = normalize(fragNormal);
    float key = max(dot(normal, -LIGHT_DIR), 0.0);
    float light = AMBIENT + KEY*key;

    vec3 shaded = pow(texelColor.rgb*light, vec3(LIFT));

    finalColor = vec4(shaded, texelColor.a)*colDiffuse*fragColor;
}
