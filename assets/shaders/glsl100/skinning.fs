#version 100

precision mediump float;

// Input vertex attributes (from vertex shader)
varying vec2 fragTexCoord;
varying vec4 fragColor;
varying vec3 fragNormal;

// Input uniform values
uniform sampler2D texture0;
uniform vec4 colDiffuse;

// Kept in step with glsl330/skinning.fs -- see the comment there for what these
// do and which one to turn to brighten the character.
// GLSL ES 100 has no normalize() in a const initializer, so it is done in main.
const vec3 LIGHT_DIR = vec3(-0.35, -1.0, -0.55);
const float AMBIENT = 1.15;
const float KEY = 0.60;
const float LIFT = 0.78;

void main()
{
    // Fetch color from texture sampler
    vec4 texelColor = texture2D(texture0, fragTexCoord);

    vec3 normal = normalize(fragNormal);
    float key = max(dot(normal, -normalize(LIGHT_DIR)), 0.0);
    float light = AMBIENT + KEY*key;

    vec3 shaded = pow(texelColor.rgb*light, vec3(LIFT));

    // Calculate final fragment color
    gl_FragColor = vec4(shaded, texelColor.a)*colDiffuse*fragColor;
}
