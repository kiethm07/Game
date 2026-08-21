#version 330

// The campfire flame: unlit, and drawn with BLEND_ADDITIVE.
//
// UNLIT, because fire is a light source. Run through level.fs it would be
// multiplied by AMBIENT + KEY*key like a rock, so the flame would go *dimmer*
// in shade and read as painted orange plastic. Here the colour goes out at
// full strength wherever the fire is, which is what makes it look like it is
// producing the light rather than receiving it.
//
// ADDITIVE, because the asset's flame is four nested closed shells -- red
// outside, then orange, then yellow, with a white core -- and drawn opaque the
// red one hides the other three completely. Adding them instead means they
// accumulate: the middle, where all four overlap, sums to hot near-white, and
// the fringe where only red reaches stays deep red. That is the gradient the
// artist's preview shows, and addition is commutative so it needs no depth
// sort -- which matters, because concentric shells have no meaningful
// back-to-front order.
//
// raylib's BLEND_ADDITIVE is glBlendFunc(GL_SRC_ALPHA, GL_ONE), so the alpha
// written below is the weight each shell contributes with. The .fbx author set
// those per shell (red 0.17, orange 0.48, yellow 0.43, white 0.31) for an
// alpha-composited preview, and they carry over as sensible additive weights:
// the big outer shell contributes least, the small core most.

out vec4 finalColor;

uniform vec4 colDiffuse;

/// Overall brightness. The authored alphas were tuned for alpha compositing,
/// where they sum to 1 by construction; under addition they are free to go
/// past that, and a little over-drive is what makes the core read as hot
/// rather than merely pale. Turn it down if the fire blows out on screen.
const float FLAME_GAIN = 1.35;

void main()
{
    finalColor = vec4(colDiffuse.rgb*FLAME_GAIN, colDiffuse.a);
}
