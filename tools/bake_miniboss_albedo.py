"""Flatten the Blood Knight's PBR set into ONE albedo the game's shader can show.

assets/shaders/glsl330/skinning.fs samples exactly one texture -- `texture0`,
the glTF baseColorTexture -- and lights it with a lambert term and a midtone
lift. It reads no metallic, no roughness, no normal map and no emissive. So on
this character every bit of the "polished fluted armour" reading lived in maps
the game throws away, and what shipped was the raw `drakulColor.png`: a narrow
band of dark maroon with pale yellow-green hairlines. Flat, muddy, and noisy at
distance, which is exactly what it looked like in game.

The Paladin -- the asset that DOES read well under this shader -- shows why.
Its diffuse carries the shading baked in: near-black recesses against light
grey plate, grime, scratches, and gold trim that is thin and DARK. Value
contrast is authored into the texture rather than left to the material.

This script does the same thing to the Blood Knight, in texture space rather
than through a UV bake -- every input map is 2048x2048 on the same UV layout,
so a per-texel composite is exact and cannot introduce seams:

    * cavity from the normal map's divergence, so the fluting reads as ridges
      and crevices instead of a flat sheet
    * a fixed-direction lambert + Blinn specular, weighted by the metallic and
      smoothness maps, so polished plate carries a sheen and cloth does not.
      Baked-in light is normally a mistake; here the alternative is no shape
      information at all, and it is kept low-contrast enough to sit under the
      engine's own key light rather than fight it.
    * ambient occlusion multiplied in, darkening both diffuse and specular
    * the emissive map added last and unoccluded, because the helm's eye slits
      are emissive-only -- without this they go dark, since the game samples no
      emissive channel
    * the yellow-green trim pulled down to a dark tarnished ochre. It is not
      removed: at 2048 across a 2.6 m character those hairlines are near
      sub-pixel, and a bright thin line that small aliases into crawling noise.
      Dark trim reads as trim. See TRIM_* below.
    * a crimson grade and an S-curve, for the messy-bloody-red look, with dried
      blood pooled into the recesses the AO and cavity maps already identify
    * the UV padding rebuilt by dilating the islands outward

That last one is the other half of the "noisy" complaint, and it is not a trim
problem at all. `drakulColor.png`'s gutters are filled with a pale yellow-green
that matches nothing on the model. Those texels are outside every UV island, so
they are invisible on mip 0 -- but each mip level averages them into the island
edges, so at any distance the armour picks up a crawling yellow fringe. Padding
has to be an extension of the island beside it, which is what DILATE_PASSES
does; the source map simply was not authored that way.

The grade is calibrated against the Paladin, measured rather than eyeballed.
Its diffuse sits at median luminance 0.151 with mean saturation 0.071. Rather
than hard-code an exposure to match that, the composite's own median (inside
the UV coverage only -- padding must not drag the statistic) is measured and
scaled onto TARGET_MEDIAN_LUM, so retuning cavity or specular cannot silently
darken the whole character. Contrast is then applied about that same measured
median, not a fixed pivot: a fixed one at 0.34 against this texture's 0.04
median crushed everything below it to black, which is how the first pass came
out darker than the mud it was replacing.

Deliberately left a little under the Paladin's median: skinning.fs applies
AMBIENT 1.15 plus a key and then pow(x, 0.78), which lifts midtones hard, and
a saturated red reads brighter than its luminance suggests.

Usage:
    blender --background <pack_miniboss.blend> \
        --python tools/bake_miniboss_albedo.py -- <out.png>

The .blend is opened only to read the skin's UV layout for the coverage mask;
nothing in it is modified.
"""

import os
import sys

import bpy
import numpy as np

TEX_DIR = ("/Users/long/Documents/3D/Model/knight-of-the-blood-order/textures/")

MAPS = {
    "color": "drakulColor.png",
    "metallic": "drakulMetallic.png",
    "smoothness": "drakulSmoothness.png",
    "normal": "drakulNormal.png",
    "ao": "1Ambient_Occlusion.png",
    "emissive": "drakulemissive.png",
}

# --- shape -----------------------------------------------------------------
# The baked key. Tangent space, so +Z is "out of the surface": this is an
# upper-left-ish light, which is the convention every hand-painted game texture
# is lit from and reads as natural.
LIGHT_DIR = (-0.35, 0.45, 0.82)

# Diffuse wrap. `shape` spans SHAPE_BASE .. SHAPE_BASE+SHAPE_GAIN, so nothing
# goes fully black -- a baked shadow that hits zero cannot be lit back up by
# the engine's key and reads as a hole.
SHAPE_BASE = 0.72
SHAPE_GAIN = 0.52

# How hard the normal map's divergence pushes ridges up and crevices down.
# This is what makes the fluting legible; it is the single most important
# number here.
CAVITY_GAIN = 9.0
# Rings of texels next to a UV seam that get no cavity at all. See erode().
CAVITY_EDGE_GUARD = 2
CAVITY_DIFFUSE = 0.55

AO_STRENGTH = 0.85

# --- specular --------------------------------------------------------------
# Blinn exponent runs SPEC_TIGHT_MIN..MAX across the smoothness map, so cloth
# gets a broad dull sheen and plate a tight bright one.
SPEC_TIGHT_MIN = 8.0
SPEC_TIGHT_MAX = 140.0
SPEC_GAIN = 0.85
# What a non-metal still reflects. Metals get the rest, tinted by their own
# albedo, which is what makes metal look like metal rather than white-hot.
SPEC_DIELECTRIC = 0.25
# Metal's specular tint is its albedo, but this albedo is very dark; used raw
# the sheen barely registers. Brightened toward its own hue instead.
SPEC_METAL_TINT_LIFT = 3.2

# --- trim ------------------------------------------------------------------
# The trim is picked out by being green-relative-to-red and bright, against an
# albedo that is otherwise almost pure dark red. Soft masks, not a threshold,
# so the edit cannot leave a hard outline of its own.
TRIM_GR_LO, TRIM_GR_HI = 0.42, 0.78
TRIM_LUM_LO, TRIM_LUM_HI = 0.015, 0.085
TRIM_STRENGTH = 0.92
# Dark tarnished ochre, roughly the Paladin's trim value.
TRIM_COLOR = (0.115, 0.062, 0.018)
TRIM_DARKEN = 0.42
# Trim is the smoothest, most metallic thing on the model, so the specular
# term lands hardest exactly on the hairlines that were already too loud.
# Suppressed there specifically rather than by weakening SPEC_GAIN globally,
# which would flatten the plate the specular is carrying.
TRIM_SPEC_SUPPRESS = 0.80

# --- blood -----------------------------------------------------------------
# Blood collects where the geometry is occluded, which the AO and cavity maps
# already describe, so no hand-painted mask is needed.
BLOOD_FROM_AO = 1.15
BLOOD_FROM_CAVITY = 0.85
BLOOD_NOISE_GAIN = 0.62
# Patchy soiling across the whole surface, so the plate is not uniformly clean
# between its recesses -- this is most of what reads as "messy".
GRIME_MOTTLE = 0.44
BLOOD_AMOUNT = 0.80
BLOOD_DARK = (0.030, 0.0035, 0.0028)
BLOOD_FRESH = (0.140, 0.0085, 0.0060)
# Mottling. UV-space noise, so it can step across an island boundary -- kept
# low-amplitude and low-frequency for that reason.
NOISE_OCTAVES = 4
NOISE_BASE_FREQ = 10
NOISE_SEED = 7

# --- grade -----------------------------------------------------------------
# Hue push applied before the auto-exposure, so it shifts colour without
# also changing overall level -- the exposure below corrects for it.
CRIMSON = (1.28, 0.56, 0.54)
# Chroma multiplier about luminance. Above 1.0 on purpose: pow(x, 0.78)
# compresses toward white and desaturates as it lifts, so chroma has to be
# banked in the texture to survive it.
SATURATION = 0.95
EMISSIVE_GAIN = 2.6
# NOT the Paladin's median (0.151), which was the first pass's mistake. That
# reference is about technique -- baked contrast, dark trim -- not about level:
# the Paladin is grey steel, and a midtone grey is what grey steel should be.
# Solved for the look instead. The shader displays pow(tex*light, 0.78) with
# light 1.15..1.75, so a deep blood red of about (0.35, 0.10, 0.08) on screen
# needs roughly (0.19, 0.04, 0.03) stored -- median luminance near 0.07. Author
# it at the Paladin's level and the lift turns it into pale terracotta, which
# is exactly what the first attempt rendered.
TARGET_MEDIAN_LUM = 0.072
CONTRAST = 1.28
# How far the padding is grown out of the islands. 32 px on a 2048 map keeps
# the gutters clean several mip levels down, which is where the fringe showed.
DILATE_PASSES = 48

SKIN_OBJECT = "mesh"


def srgb_to_linear(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * (c ** (1.0 / 2.4)) - 0.055)


def smoothstep(lo, hi, x):
    t = np.clip((x - lo) / max(hi - lo, 1e-9), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def load_raw(name):
    """Read a map's stored values verbatim, with no colour management applied.

    Every input is forced to Non-Color before its pixels are touched. Blender
    would otherwise hand back scene-linear floats for the sRGB-tagged maps and
    raw ones for the data maps, i.e. two different conventions in the same
    array set; doing the sRGB decode explicitly below keeps it one.
    """
    path = os.path.join(TEX_DIR, MAPS[name])
    img = bpy.data.images.load(path, check_existing=False)
    img.colorspace_settings.name = "Non-Color"
    w, h = img.size
    buf = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(buf)
    bpy.data.images.remove(img)
    return buf.reshape(h, w, 4)


def value_noise(h, w, freq, seed):
    rng = np.random.default_rng(seed)
    g = rng.random((freq + 1, freq + 1)).astype(np.float32)
    ys = np.linspace(0, freq, h, endpoint=False, dtype=np.float32)
    xs = np.linspace(0, freq, w, endpoint=False, dtype=np.float32)
    y0 = np.floor(ys).astype(np.int32)
    x0 = np.floor(xs).astype(np.int32)
    fy = ys - y0
    fx = xs - x0
    fy = (fy * fy * (3.0 - 2.0 * fy))[:, None]
    fx = (fx * fx * (3.0 - 2.0 * fx))[None, :]
    g00 = g[np.ix_(y0, x0)]
    g01 = g[np.ix_(y0, x0 + 1)]
    g10 = g[np.ix_(y0 + 1, x0)]
    g11 = g[np.ix_(y0 + 1, x0 + 1)]
    top = g00 * (1.0 - fx) + g01 * fx
    bot = g10 * (1.0 - fx) + g11 * fx
    return top * (1.0 - fy) + bot * fy


def uv_coverage(h, w):
    """Boolean mask of the texels any triangle of the skin actually lands on.

    Rasterised from the mesh's own UVs rather than guessed from the image,
    because the thing being separated -- gutter from island -- is a property of
    the layout, not of the colours. Blender's image rows run bottom-up and UV v
    runs bottom-up too, so v maps to the row index directly with no flip.
    """
    obj = bpy.data.objects[SKIN_OBJECT]
    me = obj.data
    me.calc_loop_triangles()

    uvs = np.empty(len(me.loops) * 2, dtype=np.float32)
    me.uv_layers.active.data.foreach_get("uv", uvs)
    uvs = uvs.reshape(-1, 2)

    tri_loops = np.empty(len(me.loop_triangles) * 3, dtype=np.int32)
    me.loop_triangles.foreach_get("loops", tri_loops)
    tris = uvs[tri_loops.reshape(-1, 3)]

    px = np.empty_like(tris)
    px[..., 0] = tris[..., 0] * w
    px[..., 1] = tris[..., 1] * h

    mask = np.zeros((h, w), dtype=bool)
    for tri in px:
        x0 = max(int(np.floor(tri[:, 0].min())) - 1, 0)
        x1 = min(int(np.ceil(tri[:, 0].max())) + 1, w)
        y0 = max(int(np.floor(tri[:, 1].min())) - 1, 0)
        y1 = min(int(np.ceil(tri[:, 1].max())) + 1, h)
        if x1 <= x0 or y1 <= y0:
            continue
        ys, xs = np.mgrid[y0:y1, x0:x1]
        xs = xs + 0.5
        ys = ys + 0.5
        (ax, ay), (bx, by), (cx, cy) = tri
        den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(den) < 1e-12:
            continue
        u = ((by - cy) * (xs - cx) + (cx - bx) * (ys - cy)) / den
        v = ((cy - ay) * (xs - cx) + (ax - cx) * (ys - cy)) / den
        inside = (u >= 0.0) & (v >= 0.0) & (u + v <= 1.0)
        mask[y0:y1, x0:x1] |= inside
    return mask


def erode(mask, passes):
    """Shrink a mask by `passes` rings -- a texel survives only if all 8 of its
    neighbours are set.

    Used to keep the cavity term away from island borders. The normal map is
    discontinuous across a UV seam, so the finite difference that measures
    curvature reads that seam as near-infinite curvature and paints a bright
    rim around every island -- which looked exactly like the yellow fringe this
    script exists to remove.
    """
    out = mask.copy()
    shifts = ((1, 0), (-1, 0), (0, 1), (0, -1),
              (1, 1), (1, -1), (-1, 1), (-1, -1))
    for _ in range(passes):
        keep = out.copy()
        for dy, dx in shifts:
            keep &= np.roll(np.roll(out, dy, 0), dx, 1)
        out = keep
    return out


def dilate(rgb, mask, passes):
    """Grow the covered texels outward into the gutters, one ring per pass."""
    out = rgb.copy()
    out[~mask] = 0.0
    filled = mask.copy()
    shifts = ((1, 0), (-1, 0), (0, 1), (0, -1),
              (1, 1), (1, -1), (-1, 1), (-1, -1))
    for _ in range(passes):
        if filled.all():
            break
        acc = np.zeros_like(out)
        cnt = np.zeros(filled.shape, dtype=np.float32)
        for dy, dx in shifts:
            acc += np.roll(np.roll(out, dy, 0), dx, 1) \
                * np.roll(np.roll(filled, dy, 0), dx, 1)[..., None]
            cnt += np.roll(np.roll(filled, dy, 0), dx, 1)
        newly = (~filled) & (cnt > 0)
        out[newly] = acc[newly] / cnt[newly][..., None]
        filled |= newly
    return out


def fbm(h, w):
    total = np.zeros((h, w), dtype=np.float32)
    amp, freq, norm = 1.0, NOISE_BASE_FREQ, 0.0
    for octave in range(NOISE_OCTAVES):
        total += amp * value_noise(h, w, freq, NOISE_SEED + octave)
        norm += amp
        amp *= 0.5
        freq *= 2
    return total / norm


def main():
    out_path = sys.argv[sys.argv.index("--") + 1]

    color = load_raw("color")
    h, w = color.shape[:2]
    metallic = load_raw("metallic")[..., 0]
    smooth = load_raw("smoothness")[..., 0]
    normal = load_raw("normal")[..., :3]
    ao = load_raw("ao")[..., 0]
    emissive = load_raw("emissive")[..., :3]

    albedo = srgb_to_linear(color[..., :3])
    coverage = uv_coverage(h, w)
    interior = erode(coverage, CAVITY_EDGE_GUARD).astype(np.float32)

    # ---- tame the trim, before shading so the shading applies uniformly ----
    r, g, b = albedo[..., 0], albedo[..., 1], albedo[..., 2]
    lum = 0.2126 * r + 0.7152 * g + 0.0722 * b
    gr = g / np.maximum(r, 1e-5)
    trim = (smoothstep(TRIM_GR_LO, TRIM_GR_HI, gr)
            * smoothstep(TRIM_LUM_LO, TRIM_LUM_HI, lum))
    trim_rgb = np.array(TRIM_COLOR, dtype=np.float32)[None, None, :]
    trim_target = trim_rgb * (lum[..., None] / max(float(lum.mean()), 1e-5)) \
        * TRIM_DARKEN
    albedo = albedo * (1.0 - (trim * TRIM_STRENGTH)[..., None]) \
        + np.clip(trim_target, 0.0, 1.0) * (trim * TRIM_STRENGTH)[..., None]

    # ---- geometry: cavity and the baked key ----
    n = normal * 2.0 - 1.0
    n /= np.maximum(np.linalg.norm(n, axis=2, keepdims=True), 1e-6)
    cavity = -(np.gradient(n[..., 0], axis=1) + np.gradient(n[..., 1], axis=0))
    cavity = np.clip(cavity * CAVITY_GAIN, -1.0, 1.0) * interior

    L = np.array(LIGHT_DIR, dtype=np.float32)
    L /= np.linalg.norm(L)
    H = L + np.array([0.0, 0.0, 1.0], dtype=np.float32)
    H /= np.linalg.norm(H)
    ndl = np.clip((n * L).sum(axis=2), 0.0, 1.0)
    ndh = np.clip((n * H).sum(axis=2), 0.0, 1.0)

    ao_term = 1.0 - AO_STRENGTH * (1.0 - ao)
    shape = (SHAPE_BASE + SHAPE_GAIN * ndl) * (1.0 + CAVITY_DIFFUSE * cavity)
    out = albedo * shape[..., None] * ao_term[..., None]

    # ---- specular ----
    tight = SPEC_TIGHT_MIN + (SPEC_TIGHT_MAX - SPEC_TIGHT_MIN) * smooth
    spec = ndh ** tight
    amount = SPEC_GAIN * smooth * (SPEC_DIELECTRIC
                                   + (1.0 - SPEC_DIELECTRIC) * metallic)
    amount *= 1.0 - TRIM_SPEC_SUPPRESS * trim
    metal_tint = np.clip(albedo * SPEC_METAL_TINT_LIFT, 0.0, 1.0)
    spec_col = (1.0 - metallic[..., None]) + metal_tint * metallic[..., None]
    out += spec_col * (spec * amount * ao_term)[..., None]

    # ---- blood ----
    grime = fbm(h, w)
    recess = np.clip(BLOOD_FROM_AO * (1.0 - ao)
                     + BLOOD_FROM_CAVITY * np.clip(-cavity, 0.0, 1.0), 0.0, 1.0)
    mask = np.clip(recess * (0.55 + BLOOD_NOISE_GAIN * grime), 0.0, 1.0) \
        * BLOOD_AMOUNT
    blood = (np.array(BLOOD_DARK, dtype=np.float32)[None, None, :]
             * (1.0 - grime[..., None])
             + np.array(BLOOD_FRESH, dtype=np.float32)[None, None, :]
             * grime[..., None])
    out = out * (1.0 - mask[..., None]) + blood * mask[..., None]

    # Patchy soiling over everything, recessed or not.
    out *= (1.0 - GRIME_MOTTLE * (1.0 - grime))[..., None]

    # ---- grade: hue, then desaturate, then auto-exposure ----
    out *= np.array(CRIMSON, dtype=np.float32)[None, None, :]
    grey = (0.2126 * out[..., 0] + 0.7152 * out[..., 1]
            + 0.0722 * out[..., 2])[..., None]
    out = grey + (out - grey) * SATURATION

    # Exposure is solved on the covered texels only, in the sRGB space the
    # target was measured in -- matching a median through a nonlinear encode
    # is not the same as matching it before one.
    lum = 0.2126 * out[..., 0] + 0.7152 * out[..., 1] + 0.0722 * out[..., 2]
    median_lin = float(np.median(lum[coverage]))
    target_lin = float(srgb_to_linear(np.array(TARGET_MEDIAN_LUM)))
    exposure = target_lin / max(median_lin, 1e-6)
    out *= exposure

    # ---- eyes: emissive is not sampled in game, so it has to live here ----
    # After the exposure solve, so two glowing eye slits cannot pull the whole
    # character's level around, and unoccluded because they are their own light.
    out += srgb_to_linear(emissive) * EMISSIVE_GAIN

    srgb = linear_to_srgb(out)
    pivot = float(np.median(srgb[coverage]))
    srgb = np.clip((srgb - pivot) * CONTRAST + pivot, 0.0, 1.0)

    srgb = dilate(srgb.astype(np.float32), coverage, DILATE_PASSES)

    covered_lum = (0.2126 * srgb[..., 0] + 0.7152 * srgb[..., 1]
                   + 0.0722 * srgb[..., 2])[coverage]
    sat = (srgb.max(axis=2) - srgb.min(axis=2))[coverage]
    print("BAKE_CALIBRATION exposure=%.3f pivot=%.4f coverage=%.1f%% "
          "median=%.4f mean=%.4f p25=%.4f p75=%.4f p95=%.4f sat=%.4f"
          % (exposure, pivot, 100.0 * coverage.mean(),
             float(np.median(covered_lum)), float(covered_lum.mean()),
             *np.percentile(covered_lum, [25, 75, 95]), float(sat.mean())))

    rgba = np.concatenate(
        [srgb.astype(np.float32), np.ones((h, w, 1), dtype=np.float32)], axis=2)

    img = bpy.data.images.new("MiniBoss_albedo", width=w, height=h,
                              alpha=False, float_buffer=False)
    # Non-Color on the way out too: `srgb` already holds sRGB-encoded values,
    # so Blender must write them through untouched rather than encoding twice.
    img.colorspace_settings.name = "Non-Color"
    img.pixels.foreach_set(rgba.reshape(-1))
    img.filepath_raw = out_path
    img.file_format = "PNG"
    img.save()

    print("BAKE_ALBEDO_RESULT %s %dx%d  mean=%.4f  p01=%.4f p99=%.4f"
          % (out_path, w, h, float(srgb.mean()),
             float(np.percentile(srgb, 1)), float(np.percentile(srgb, 99))))


if __name__ == "__main__":
    main()
