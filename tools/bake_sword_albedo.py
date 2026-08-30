"""Paint the miniboss greatsword's albedo: old steel, rust, and dried blood.

    blender -b ~/Documents/3D/Model/pack_miniboss.blend \
        --python tools/bake_sword_albedo.py -- source/models/MiniBoss_sword_albedo.png

Runs AFTER tools/add_miniboss_sword.py, which is what leaves the prop at final
scale with a UV island of its own to paint into.

Why the sword is painted rather than composited
-----------------------------------------------
tools/bake_miniboss_albedo.py had a PBR set to flatten: four 2048 maps on a
shared layout, so the Blood Knight's albedo is a per-texel composite of maps
that already existed. This prop has nothing to composite. It arrived as Mixamo's
"Maria W/Prop J J Ong", and its 42 vertices address a 2.4% sliver of *Maria's
character sheet* -- a texture of a woman's face and clothes, whose remaining
97.6% is not shipping and whose pixels do not survive the FBX unpack anyway
(all three maps come back `has_data False`).

So the albedo is generated. Every texel is projected back to its position on the
sword and shaded from that -- which is also why the UV stretch add_miniboss_sword
applies is harmless: the noise is evaluated in METRES along the blade, not in UV
space, so nothing distorts when the island is stretched to fill the map.

What it paints, and why it is dark
----------------------------------
assets/shaders/glsl330/skinning.fs samples exactly one texture and lights it
with AMBIENT 1.15, a lambert key, and a final pow(x, 0.78). That lifts midtones
hard, so an albedo authored at the value it should APPEAR ships far too bright.
The Blood Knight himself is graded to a median luminance of 0.072 for this
reason; STEEL_* below sits in the same range deliberately, and the honed edge --
the brightest thing on the weapon -- is still only 0.52.

Four things are layered, in the order a real blade would acquire them:

  * tarnish -- low-frequency mottling between STEEL_DARK and STEEL_MID, plus a
    darker band down the middle of each face where a fuller would be
  * edge wear -- the one BRIGHT element, keyed off the surface normal rather
    than off any measured "which side is sharp". This blade is double-edged, so
    both edges qualify, and `|n . edge_axis|` finds them on either side without
    the convex-side test add_katana.py needs for a single-edged katana
  * rust -- thresholded mid-frequency noise, weighted onto the guard, the
    pommel and the spine, i.e. the places water sits and a whetstone never
    reaches. Never over the honed edge
  * blood -- dried, thresholded from noise that is deliberately ANISOTROPIC:
    high frequency across the blade and low along it, which makes streaks that
    run with the blade instead of blotches. Weighted to the ricasso (where it
    runs down to and dries against the guard), to the tip, and to the edge

BLOOD_COVERAGE and RUST_COVERAGE are targets, not thresholds: the cut is solved
for per run so that retuning the weighting cannot silently turn "a little bit
bloody" into a red sword. The measured result is printed.
"""

import os
import sys

import bpy
import numpy as np
from mathutils import Vector

SWORD = "MiniBoss_Sword"
MATERIAL = "MiniBoss_Sword"
SIZE = (256, 1024)          # (width, height); matches UV_IMAGE_SIZE

# sRGB-encoded, not linear -- see the module docstring on why these are so dark.
STEEL_DARK = (0.058, 0.063, 0.073)
STEEL_MID = (0.126, 0.133, 0.148)
STEEL_EDGE = (0.330, 0.340, 0.360)
RUST_DEEP = (0.098, 0.044, 0.022)
RUST_BLOOM = (0.152, 0.082, 0.044)
BLOOD_DRY = (0.098, 0.020, 0.017)
BLOOD_CRUST = (0.196, 0.030, 0.024)
LEATHER = (0.060, 0.045, 0.034)
LEATHER_WORN = (0.116, 0.089, 0.066)
FITTING = (0.115, 0.086, 0.043)   # tarnished brass on guard and pommel

# Fractions of the covered texels each stain is solved onto. "A little bit
# bloody": blood is measured over the BLADE only, since a bloody grip and a
# bloody blade read as very different amounts of blood.
BLOOD_COVERAGE = 0.18
RUST_COVERAGE = 0.26

# Where the honed strip begins, as a fraction of the blade's half-width. The
# blade is a lozenge whose faces run ridge-to-edge, so this is a real distance
# along the face rather than a normal test -- see profile().
EDGE_START = 0.86

# How much fatter than the narrowest grip section a cross-section has to be
# before profile() counts it as part of the guard rather than the grip.
GRIP_THICKNESS_TOLERANCE = 1.5

# Rings of padding grown outward from the island. Every mip level averages the
# gutter into the island edge, so unpainted gutters fringe the blade at
# distance -- the same trap bake_miniboss_albedo.py documents at length.
DILATE_PASSES = 28

SEED = 0x5EC1
LATTICE = 512


def _lattice(seed):
    return np.random.default_rng(seed).random((LATTICE, LATTICE)).astype(np.float32)


def noise(x, y, freq, seed):
    """Value noise sampled at arbitrary points, in whatever unit x and y carry.

    Sample-based rather than the grid-based `value_noise` in
    bake_miniboss_albedo.py: there the noise IS the texture, so a grid over the
    image is the natural domain. Here the domain is the sword -- metres along
    the blade and across it -- and the texels sampling it are scattered through
    UV space, so the lattice has to be read at points rather than resampled.
    """
    g = _lattice(seed)
    fx, fy = np.asarray(x) * freq, np.asarray(y) * freq
    ix, iy = np.floor(fx).astype(np.int64), np.floor(fy).astype(np.int64)
    tx, ty = fx - ix, fy - iy
    tx = tx * tx * (3.0 - 2.0 * tx)
    ty = ty * ty * (3.0 - 2.0 * ty)
    i0, i1 = ix % LATTICE, (ix + 1) % LATTICE
    j0, j1 = iy % LATTICE, (iy + 1) % LATTICE
    a = g[i0, j0] * (1.0 - tx) + g[i1, j0] * tx
    b = g[i0, j1] * (1.0 - tx) + g[i1, j1] * tx
    return a * (1.0 - ty) + b * ty


def fbm(x, y, freq, seed, octaves=4):
    total = np.zeros(np.shape(x), dtype=np.float32)
    amp, norm = 1.0, 0.0
    for o in range(octaves):
        total += amp * noise(x, y, freq * (2 ** o), seed + o)
        norm += amp
        amp *= 0.5
    return total / norm


def smoothstep(lo, hi, x):
    t = np.clip((np.asarray(x) - lo) / (hi - lo), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def mix(a, b, t):
    """Blend two colours; `a`/`b` may be RGB triples or per-texel arrays."""
    a = np.asarray(a, dtype=np.float32)
    b = np.asarray(b, dtype=np.float32)
    t = np.asarray(t, dtype=np.float32)[..., None] if np.ndim(t) else t
    return a * (1.0 - t) + b * t


def rasterize(obj, w, h):
    """Project every texel of the island back onto the mesh.

    Returns world position, interpolated normal and a coverage mask. Loop
    normals rather than face normals: the edge bevels are the whole point of the
    `edge` term below, and flat-shaded facets would step it instead of ramping
    it across the bevel.
    """
    me = obj.data
    me.calc_loop_triangles()
    uvs = np.array([d.uv[:] for d in me.uv_layers.active.data], dtype=np.float64)
    co = np.array([list(obj.matrix_world @ v.co) for v in me.vertices])
    nrm_m = obj.matrix_world.to_3x3().inverted().transposed()
    lnrm = np.array([list((nrm_m @ Vector(l.normal)).normalized()) for l in me.loops])

    pos = np.zeros((h, w, 3), dtype=np.float32)
    nor = np.zeros((h, w, 3), dtype=np.float32)
    mask = np.zeros((h, w), dtype=bool)

    for tri in me.loop_triangles:
        li = list(tri.loops)
        uv = uvs[li] * np.array([w, h])
        p = co[list(tri.vertices)]
        n = lnrm[li]

        x0 = max(int(np.floor(uv[:, 0].min())) - 1, 0)
        x1 = min(int(np.ceil(uv[:, 0].max())) + 1, w)
        y0 = max(int(np.floor(uv[:, 1].min())) - 1, 0)
        y1 = min(int(np.ceil(uv[:, 1].max())) + 1, h)
        if x1 <= x0 or y1 <= y0:
            continue
        xs, ys = np.meshgrid(np.arange(x0, x1) + 0.5, np.arange(y0, y1) + 0.5)

        (ax, ay), (bx, by), (cx, cy) = uv
        det = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(det) < 1e-12:
            continue
        l0 = ((by - cy) * (xs - cx) + (cx - bx) * (ys - cy)) / det
        l1 = ((cy - ay) * (xs - cx) + (ax - cx) * (ys - cy)) / det
        l2 = 1.0 - l0 - l1
        # A half-texel of slack: a texel whose centre falls just outside a
        # triangle it borders still has to be painted, or every seam inside the
        # island shows as an unpainted crack that dilation then smears.
        eps = -0.5 / max(w, h)
        inside = (l0 >= eps) & (l1 >= eps) & (l2 >= eps)
        if not inside.any():
            continue

        ys_i, xs_i = np.nonzero(inside)
        gy, gx = ys_i + y0, xs_i + x0
        # First triangle to claim a texel keeps it. Overlapping claims only
        # happen in the half-texel slack above, where either answer is right.
        fresh = ~mask[gy, gx]
        if not fresh.any():
            continue
        bary = np.stack([l0[inside], l1[inside], l2[inside]], axis=-1)[fresh]
        pos[gy[fresh], gx[fresh]] = bary @ p
        nor[gy[fresh], gx[fresh]] = bary @ n
        mask[gy[fresh], gx[fresh]] = True

    ln = np.linalg.norm(nor, axis=2, keepdims=True)
    nor = np.divide(nor, np.maximum(ln, 1e-9))
    return pos, nor, mask


def profile(obj, axes):
    """Split the sword into pommel / grip / guard / blade, off its own rings.

    This mesh is 42 vertices arranged as a dozen cross-sections, and both the
    landmarks below come out of them rather than out of constants:

      * `guard_peak` is the widest section -- the crossguard arms, at 0.26 m
        half-width against a 0.021 m grip. The same "widest cross-section is the
        guard" test add_katana.py uses to find a tsuba.
      * `grip_top` and `pommel_end` are found by walking outward from there and
        from the butt until a section drops back to grip thickness. Radius alone
        cannot separate guard from blade -- the blade is genuinely wider than
        the grip -- but the guard is a contiguous BULGE, and walking it is what
        that distinction needs.

    Also returns the blade's half-width sampled along its length, which is what
    `edge` is measured against below. That has to come from the silhouette: on a
    lozenge section this low-poly the smooth-shaded normal at an edge vertex
    points sideways and is then interpolated all the way to the ridge, so
    `|n . edge_axis|` reads as "edge" across half of every face. Keying the
    honed edge off it washed the whole blade out.
    """
    co = np.array([list(obj.matrix_world @ v.co) for v in obj.data.vertices])
    d = co - np.array(axes["centre"])
    t = d @ np.array(axes["axis"])
    s = (t - axes["t_lo"]) if axes["tip_t"] > axes["grip_end_t"] else (axes["t_hi"] - t)
    across = np.abs(d @ np.array(axes["edge_axis"]))
    radius = np.linalg.norm(d - np.outer(t, np.array(axes["axis"])), axis=1)

    key = np.round(s, 4)
    ring_s = np.unique(key)
    ring_r = np.array([radius[key == u].max() for u in ring_s])
    ring_a = np.array([across[key == u].max() for u in ring_s])

    peak = int(np.argmax(ring_r))
    inner = [i for i in range(1, peak) if ring_r[i] > 0.0]
    r_grip = min(ring_r[i] for i in inner) if inner else ring_r[peak]
    thick = r_grip * GRIP_THICKNESS_TOLERANCE

    i = peak
    while i > 0 and ring_r[i - 1] > thick:
        i -= 1
    grip_top = float(ring_s[i])
    j = 0
    while j + 1 < peak and ring_r[j + 1] > thick:
        j += 1
    pommel_end = float(ring_s[j])

    # Sampled from the sections that actually carry an edge; the ridge-only
    # sections read zero half-width and would collapse the taper if included.
    blade = ring_s >= ring_s[peak]
    keep = blade & (ring_a >= 0.25 * ring_a[blade].max())
    xs = np.append(ring_s[keep], float(ring_s[-1]))
    ys = np.append(ring_a[keep], 0.0)
    order = np.argsort(xs)

    return {"pommel_end": pommel_end, "grip_top": grip_top,
            "guard_peak": float(ring_s[peak]),
            "half_s": xs[order], "half_w": ys[order],
            "grip_radius": float(r_grip)}


def solve_threshold(field, mask, coverage):
    """The cut that puts exactly `coverage` of `mask` above it."""
    if not mask.any():
        return 1.0
    return float(np.quantile(field[mask], 1.0 - coverage))


def shade(pos, nor, mask, axes, prof):
    """Colour every covered texel from where it sits on the sword."""
    axis = np.array(axes["axis"])
    d = pos - np.array(axes["centre"])
    t = d @ axis
    s = (t - axes["t_lo"]) if axes["tip_t"] > axes["grip_end_t"] else (axes["t_hi"] - t)
    across = d @ np.array(axes["edge_axis"])
    flat = d @ np.array(axes["flat_axis"])
    L = axes["length"]

    is_pommel = s < prof["pommel_end"]
    is_grip = (s >= prof["pommel_end"]) & (s < prof["grip_top"])
    is_guard = (s >= prof["grip_top"]) & (s <= prof["guard_peak"])
    is_blade = s > prof["guard_peak"]
    blade_t = np.clip((s - prof["guard_peak"]) / max(L - prof["guard_peak"], 1e-6),
                      0.0, 1.0)

    # How far across the blade this texel sits, as a fraction of the blade's own
    # half-width THERE -- 0 on the ridge, 1 on the cutting edge. See profile().
    half = np.interp(s, prof["half_s"], prof["half_w"])
    edge = np.clip(np.abs(across) / np.maximum(half, 1e-6), 0.0, 1.0) * is_blade

    # The two faces must not paint identically -- mirrored streaks read as a
    # pattern. Offsetting the noise domain by face decorrelates them.
    face = np.where(flat >= 0.0, 0.0, 37.0)

    # --- tarnished steel
    grain = fbm(s * 3.1, across * 3.1 + face, 1.0, SEED, octaves=5)
    col = mix(STEEL_DARK, STEEL_MID, grain)
    # Grime collects in the hollow between the ridge and the edge, and neither
    # a whetstone nor a sleeve reaches it.
    hollow = np.exp(-(((edge - 0.45) / 0.28) ** 2)) * is_blade
    col *= (1.0 - 0.35 * hollow)[..., None]

    # --- honed edge: the one bright element, and a narrow one
    wear = smoothstep(EDGE_START, 0.99, edge)
    wear = wear * (0.45 + 0.55 * fbm(s * 7.0, across * 22.0 + face, 1.0, SEED + 40))
    col = mix(col, STEEL_EDGE, wear)

    # --- rust, where water sits and a stone never reaches
    rust_n = fbm(s * 9.0, across * 9.0 + face, 1.0, SEED + 80, octaves=5)
    rust_w = (0.35
              + 0.80 * is_guard + 0.70 * is_pommel + 0.30 * is_grip
              + 0.60 * (1.0 - smoothstep(0.0, 0.22, blade_t)) * is_blade
              + 0.55 * (1.0 - edge) * is_blade)
    rust_f = rust_n * rust_w * (1.0 - 0.9 * wear)
    rust_cut = solve_threshold(rust_f, mask, RUST_COVERAGE)
    rust = smoothstep(rust_cut, rust_cut + 0.14, rust_f)
    col = mix(col, mix(RUST_DEEP, RUST_BLOOM, rust_n), rust * 0.9)

    # --- pitting
    pit = fbm(s * 34.0, across * 34.0 + face, 1.0, SEED + 120, octaves=2)
    col *= (1.0 - 0.45 * smoothstep(0.78, 0.93, pit))[..., None]

    # --- grip: a leather wrap, worn on the ridges
    wrap = 0.5 + 0.5 * np.sin((s * 60.0) + (across * 14.0))
    grip_col = mix(LEATHER, LEATHER_WORN,
                   np.clip(wrap ** 3 * (0.45 + 0.55 * grain), 0.0, 1.0))
    col = np.where(is_grip[..., None], grip_col, col)

    # --- guard and pommel: tarnished fittings, not blade steel
    fitting = mix(FITTING, RUST_DEEP, 0.30 + 0.45 * grain)
    col = np.where((is_guard | is_pommel)[..., None], fitting, col)

    # --- blood. Anisotropic on purpose: fast across, slow along -> streaks.
    streak = fbm(across * 7.0 + face, s * 1.1, 1.0, SEED + 200, octaves=4)
    blood_w = (0.45
               + 0.80 * (1.0 - smoothstep(0.02, 0.34, blade_t))   # dries at the ricasso
               + 0.55 * smoothstep(0.68, 1.0, blade_t)            # and stains the point
               + 0.12 * edge                                      # a little toward the edge
               + 0.70 * is_guard + 0.35 * is_grip)
    blood_f = streak * blood_w
    blood_cut = solve_threshold(blood_f, mask & is_blade, BLOOD_COVERAGE)
    blood = smoothstep(blood_cut, blood_cut + 0.07, blood_f)
    # A crust at the rim of each patch, dried darker toward the middle.
    crust = blood * (1.0 - smoothstep(blood_cut + 0.05, blood_cut + 0.22, blood_f))
    col = mix(col, BLOOD_DRY, blood * 0.95)
    col = mix(col, BLOOD_CRUST, crust * 0.6)

    stats = {
        "blood_on_blade": round(float(blood[mask & is_blade].mean()), 4),
        "blood_overall": round(float(blood[mask].mean()), 4),
        "rust_overall": round(float(rust[mask].mean()), 4),
        "edge_wear": round(float(wear[mask].mean()), 4),
        "texels": {k: int((mask & v).sum()) for k, v in
                   (("pommel", is_pommel), ("grip", is_grip),
                    ("guard", is_guard), ("blade", is_blade))},
    }
    return np.clip(col, 0.0, 1.0).astype(np.float32), stats


def dilate(rgb, mask, passes):
    """Grow the covered texels outward into the gutters, one ring per pass.

    The gutter starts at the island's MEAN colour rather than at black, and only
    then is dilated over. One ring per pass costs a full-image roll, so covering
    a 256-wide map from a 42%-covered island would take hundreds of them -- and
    the first version, stopping at 24, left a black field beside the blade that
    every mip level would have averaged straight back into the edge. Seeding the
    unreachable remainder with the mean makes the passes a local refinement of
    an already-safe fill instead of the only thing standing between the blade
    and a black fringe.
    """
    out = rgb.copy()
    out[~mask] = rgb[mask].mean(axis=0) if mask.any() else 0.0
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


def write_image(rgb, out_path):
    """Save the sRGB-encoded values verbatim, then re-read them AS sRGB.

    Non-Color on the way out: `rgb` already holds sRGB-encoded numbers, and
    letting Blender treat them as scene-linear would encode them a second time.
    The datablock the material then points at is a fresh load of the file tagged
    sRGB, which is what the glTF exporter needs to emit a correct
    baseColorTexture.
    """
    h, w = rgb.shape[:2]
    name = os.path.basename(out_path)
    img = bpy.data.images.get(name) or bpy.data.images.new(name, w, h, alpha=False)
    img.scale(w, h)
    img.colorspace_settings.name = "Non-Color"
    rgba = np.concatenate([rgb, np.ones((h, w, 1), dtype=np.float32)], axis=2)
    img.pixels.foreach_set(rgba.reshape(-1))
    img.filepath_raw = out_path
    img.file_format = "PNG"
    img.save()
    bpy.data.images.remove(img)

    out = bpy.data.images.load(out_path, check_existing=False)
    out.colorspace_settings.name = "sRGB"
    return out


def apply_material(obj, image):
    """One image into Base Color, and nothing else.

    Every slot the prop arrived with is dropped rather than repointed: they are
    Maria's, they carry three dangling 2048 maps that unpacked to nothing, and
    the exporter would ship whichever of them survived.
    """
    dropped = [m.name for m in obj.data.materials if m]
    obj.data.materials.clear()

    mat = bpy.data.materials.get(MATERIAL) or bpy.data.materials.new(MATERIAL)
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = image
    tex.location = (-400, 0)
    bsdf.location = (-100, 0)
    out.location = (200, 0)
    # An IMAGE into Base Color is the one linked Base Color glTF understands --
    # it becomes baseColorTexture. A procedural one would ship as white; see the
    # project note on that trap.
    nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    nt.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    bsdf.inputs["Metallic"].default_value = 0.0
    bsdf.inputs["Roughness"].default_value = 0.62
    obj.data.materials.append(mat)
    return dropped


def main():
    out_path = sys.argv[sys.argv.index("--") + 1]
    if not os.path.isabs(out_path):
        out_path = os.path.abspath(out_path)

    obj = bpy.data.objects[SWORD]
    here = os.path.dirname(os.path.abspath(__file__))
    ns = {}
    exec(open(os.path.join(here, "add_miniboss_sword.py")).read().split("if __name__")[0], ns)
    axes = ns["sword_axes"](obj)

    w, h = SIZE
    pos, nor, mask = rasterize(obj, w, h)
    if mask.mean() < 0.05:
        raise RuntimeError("the island covers only %.1f%% of the map; the UVs "
                           "were probably not renormalised" % (100 * mask.mean()))

    prof = profile(obj, axes)
    rgb, stats = shade(pos, nor, mask, axes, prof)
    rgb = dilate(rgb, mask, DILATE_PASSES)
    image = write_image(rgb, out_path)
    dropped = apply_material(obj, image)

    lum = (0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2])[mask]
    print("BAKE_SWORD_RESULT " + repr({
        "path": out_path, "size": [w, h],
        "coverage": round(float(mask.mean()), 4),
        "median_luminance": round(float(np.median(lum)), 4),
        "p1_p99": [round(float(np.percentile(lum, 1)), 4),
                   round(float(np.percentile(lum, 99)), 4)],
        "materials_dropped": dropped,
        "profile": {k: (round(v, 4) if isinstance(v, float) else v)
                    for k, v in prof.items() if not k.startswith("half_")},
        **stats,
    }))
    return stats


if __name__ == "__main__":
    main()
    if "--save" in sys.argv:
        bpy.ops.wm.save_mainfile()
        print("SAVED " + bpy.data.filepath)
