#version 330

// Shared fragment stage for both depth-only passes (depth.vs and
// skinning_depth.vs). The shadow map framebuffer has a depth attachment and no
// colour attachment, so depth is written by fixed function and this output goes
// nowhere. It exists because GLSL 330 requires the stage to be present.
out vec4 finalColor;

void main()
{
    finalColor = vec4(1.0);
}
