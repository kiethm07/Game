"""Bind the Maria greatsword into the Blood Knight's right fist, at his scale.

Runs against pack_miniboss.blend -- the RETARGETED file, where the Blood Knight
mesh is already on the 69-bone Mixamo rig. It expects the sword as the user
brought it in: Mixamo's "Maria W/Prop J J Ong" imported whole, which lands a
`Maria_sword` mesh rigid-bound to `mixamorig:RightHand` on its OWN 65-bone
Mixamo armature (`Armature.061` at import time).

    blender -b ~/Documents/3D/Model/pack_miniboss.blend \
        --python tools/add_miniboss_sword.py -- --save

or, in a live session:

    exec(open("tools/add_miniboss_sword.py").read()); report = main()

Why this is a transform transfer and not a re-derivation of the grip
-------------------------------------------------------------------
tools/add_katana.py had to *invent* a hold: its katana arrived as a loose FBX
with no rig, so the script measures the palm plane, the finger axis and the
blade's convex side to work out how a fist would close around it.

None of that is needed here, and doing it anyway would be strictly worse. The
sword already arrives held -- Mixamo posed it in Maria's hand and bound it to
her `mixamorig:RightHand` at weight 1.0. Both rigs are Mixamo rigs with the same
bone names, and the two RightHand REST FRAMES are measured here to agree to
within REST_TOLERANCE_DEG (they came out at 0.0 deg apart, differing only in
position). So the hold is already expressed in a frame the Blood Knight also
has, and moving it over is one change of basis:

    world' = Hand_knight @ Scale(s) @ Hand_maria^-1 @ world

Everything about the grip -- which way the edge faces, how far up the hilt the
fist closes, the wrist angle -- is carried across exactly, because it is stored
relative to the hand rather than in world space. The scale sits in the middle of
the sandwich so it is applied ABOUT THE WRIST JOINT: the fist stays where the
hand is and the weapon grows out of it, rather than the whole thing scaling
about the world origin and sliding out of the hand.

Picking the scale
-----------------
The two skeletons do not differ by one number -- the Blood Knight is a bulky
figure with a big upper body and comparatively short legs, so the ratio depends
entirely on what you measure:

    RightHand      1.67       Hips->Head       1.94
    RightForeArm   1.74       Hips->RightFoot  1.22
    RightArm       1.23       shoulder span    1.84

SCALE_BONE picks the hand, because the hand is the constraint that can actually
be WRONG. A grip scaled off the torso is a grip the fist does not close around;
scaled off the hand, the hilt fills this character's fist exactly as it filled
Maria's, which is the one thing a viewer reads as an error. It also happens to
agree with the whole-body reading: the sword is 0.93x Maria's standing height,
and 1.67x its length against this 2.63 m character is 0.94x his.

The result is a two-handed sword about 2.5 m long, which is deliberate for a
miniboss and is NOT free -- see the clearance figures printed by
tools/verify_miniboss_sword.py before changing SCALE_BONE.
"""

import sys

import bpy
import numpy as np
from mathutils import Matrix, Vector

MAIN_ARM = "Armature"
HAND_BONE = "mixamorig:RightHand"
SWORD_IN = "Maria_sword"
SWORD_OUT = "MiniBoss_Sword"

# The bone whose length sets the scale. See the docstring: the fist is the
# constraint, so the hand is what the weapon is sized against.
SCALE_BONE = HAND_BONE

# How far the two rigs' hand rest frames may differ before the transfer stops
# being a change of basis and starts being a guess. Measured at 0.0 deg.
REST_TOLERANCE_DEG = 2.0

# Margin left around the UV island when it is renormalised, in texels of the
# map tools/bake_sword_albedo.py writes. Two texels is enough for the bilinear
# tap at the island edge; the dilation pass there fills the rest.
UV_MARGIN_TEXELS = 2
UV_IMAGE_SIZE = (256, 1024)

PROFILE_BINS = 24


def _find(name_hint):
    """The imported sword, by name or by being the one prop-shaped mesh."""
    obj = bpy.data.objects.get(name_hint) or bpy.data.objects.get(SWORD_OUT)
    if obj:
        return obj
    props = [o for o in bpy.data.objects
             if o.type == "MESH" and len(o.vertex_groups) == 1
             and o.vertex_groups[0].name == HAND_BONE]
    if len(props) != 1:
        raise RuntimeError("expected one hand-bound prop mesh, found %d" % len(props))
    return props[0]


def _rest_hand(arm):
    """The RightHand bone's rest frame in world space."""
    return arm.matrix_world @ arm.data.bones[HAND_BONE].matrix_local


def _bone_length(arm, name):
    b = arm.data.bones[name]
    return ((arm.matrix_world @ b.tail_local) - (arm.matrix_world @ b.head_local)).length


def sword_axes(sw):
    """Long axis, guard, and which end is the grip -- measured, not assumed.

    Same reasoning as add_katana.py's `katana_axes`: an FBX join leaves the
    origin on whichever shell Blender used as the target, so no local coordinate
    here can be trusted. The difference is that this mesh is not axis-aligned in
    ANY local axis -- it is stored already rotated into Maria's hand -- so the
    long axis comes out of an SVD of the point cloud rather than off a bounding
    box. The guard is then the widest cross-section along it, exactly as the
    tsuba is for the katana, and the grip is the short side of that guard.
    """
    co = np.array([list(sw.matrix_world @ v.co) for v in sw.data.vertices])
    centre = co.mean(axis=0)
    _, sv, vt = np.linalg.svd(co - centre, full_matrices=False)
    axis, edge_axis, flat_axis = vt[0], vt[1], vt[2]

    t = (co - centre) @ axis
    radial = co - centre - np.outer(t, axis)
    r = np.linalg.norm(radial, axis=1)

    lo, hi = float(t.min()), float(t.max())
    best = None
    for i in range(PROFILE_BINS):
        a = lo + (hi - lo) * i / PROFILE_BINS
        b = lo + (hi - lo) * (i + 1) / PROFILE_BINS
        sel = (t >= a) & (t <= b)
        if not sel.any():
            continue
        if best is None or r[sel].max() > best[1]:
            best = ((a + b) / 2.0, float(r[sel].max()))
    guard_t, guard_r = best

    # Grip is the short side of the guard, blade the long one.
    grip_end, tip = (lo, hi) if (hi - guard_t) > (guard_t - lo) else (hi, lo)

    # The guard's extent: everything still fat compared with the shaft beside it.
    fat = t[r > guard_r * 0.5]
    guard_lo, guard_hi = float(fat.min()), float(fat.max())

    return {"centre": Vector(centre), "axis": Vector(axis),
            "edge_axis": Vector(edge_axis), "flat_axis": Vector(flat_axis),
            "singular": [round(float(v), 4) for v in sv],
            "t_lo": lo, "t_hi": hi, "length": hi - lo,
            "guard_t": guard_t, "guard_r": guard_r,
            "guard_lo": guard_lo, "guard_hi": guard_hi,
            "grip_end_t": grip_end, "tip_t": tip,
            "grip_length": abs(guard_t - grip_end),
            "blade_length": abs(tip - guard_t)}


def transfer(sw, knight, maria, scale):
    """Move the hold from Maria's wrist to the Blood Knight's, growing it by `scale`."""
    hm, hk = _rest_hand(maria), _rest_hand(knight)

    angle = np.degrees(hm.to_quaternion().rotation_difference(hk.to_quaternion()).angle)
    # A quaternion and its negation are the same rotation, so a perfect match can
    # report as 360 rather than 0. Fold it before comparing.
    angle = min(angle, 360.0 - angle)
    if angle > REST_TOLERANCE_DEG:
        raise RuntimeError(
            "the two rigs' RightHand rest frames differ by %.2f deg; the hold "
            "cannot be transferred by a change of basis alone" % angle)

    # About the wrist: the fist stays put and the weapon grows out of it.
    T = hk @ Matrix.Scale(scale, 4) @ hm.inverted()
    sw.parent = None
    sw.matrix_world = T @ sw.matrix_world
    return T, angle


def bind(sw, knight):
    """One bone, full weight -- the prop follows the joint and cannot deform."""
    world = sw.matrix_world.copy()
    for vg in list(sw.vertex_groups):
        sw.vertex_groups.remove(vg)
    vg = sw.vertex_groups.new(name=HAND_BONE)
    vg.add(range(len(sw.data.vertices)), 1.0, "REPLACE")
    for m in list(sw.modifiers):
        sw.modifiers.remove(m)
    mod = sw.modifiers.new(name="Armature", type="ARMATURE")
    mod.object = knight
    sw.parent = knight
    sw.matrix_parent_inverse = knight.matrix_world.inverted()
    sw.matrix_world = world
    # merge_animations.py exports exactly the meshes parented to the armature.
    for c in list(sw.users_collection):
        c.objects.unlink(sw)
    bpy.context.scene.collection.objects.link(sw)


def normalize_uvs(sw):
    """Stretch the island off Maria's atlas to fill a map of its own.

    The prop's UVs address a tall 0.085 x 0.581 sliver of a 2048 sheet shared
    with a whole character -- 2.4% of it -- and the rest of that sheet is Maria's
    skin, which is not shipping. Rescaling the island to fill its own map is what
    lets tools/bake_sword_albedo.py write 256x1024 instead of 2048x2048 for the
    same texel density.

    Stretched to fill rather than fitted with the aspect preserved, deliberately.
    Nothing is resampled here -- the albedo is GENERATED from 3D positions, not
    carried over from Maria's sheet -- so a non-uniform UV scale cannot distort
    any content. It only sets texel density, and the island's own 1:6.9 aspect
    against the map's 1:4 leaves that near-uniform anyway: about 1.2 mm/texel
    across the guard and 1.5 mm/texel along the blade.
    """
    layer = sw.data.uv_layers.active
    uv = np.array([d.uv[:] for d in layer.data])
    lo, hi = uv.min(axis=0), uv.max(axis=0)
    span = np.maximum(hi - lo, 1e-9)
    margin = np.array([UV_MARGIN_TEXELS / UV_IMAGE_SIZE[0],
                       UV_MARGIN_TEXELS / UV_IMAGE_SIZE[1]])
    scaled = margin + (uv - lo) / span * (1.0 - 2.0 * margin)
    for d, p in zip(layer.data, scaled):
        d.uv = p
    return {"was_lo": [round(float(v), 4) for v in lo],
            "was_hi": [round(float(v), 4) for v in hi],
            "fraction_of_atlas": round(float(span[0] * span[1]), 5)}


def strip_maria(maria):
    """Delete the rig the prop came in on, once nothing hangs off it.

    Not housekeeping. merge_animations.py treats every armature that is not
    `Armature` as one animation clip and RAISES on any that holds no action, so
    Maria's rig left in the file does not merely sit there unused -- it breaks
    the export outright. If it DID carry her idle it would be worse: a 62nd clip
    named `Armature.061`, on a skeleton that is not the character's.

    Targeted at the prop's own former parent rather than swept for by shape.
    Three of the real clips (Land, Fall, InjuredIdle) sit on Mixamo's reduced
    28- and 33-bone skeletons, so "the armature with the wrong bone count" is
    not Maria -- it is also three clips this asset ships.
    """
    if maria.children:
        raise RuntimeError("%s still has children: %s"
                           % (maria.name, [c.name for c in maria.children]))
    name = maria.name
    had_action = bool(maria.animation_data and maria.animation_data.action)
    bpy.data.objects.remove(maria, do_unlink=True)
    return {"name": name, "held_an_action": had_action}


def main():
    knight = bpy.data.objects[MAIN_ARM]
    sw = _find(SWORD_IN)
    maria = sw.parent
    if maria is None or maria.type != "ARMATURE":
        raise RuntimeError("%s is not parented to an armature" % sw.name)
    if maria is knight:
        raise RuntimeError("%s is already bound to %s" % (sw.name, MAIN_ARM))

    before = sword_axes(sw)
    maria_rest = maria
    scale = _bone_length(knight, SCALE_BONE) / _bone_length(maria, SCALE_BONE)

    T, rest_angle = transfer(sw, knight, maria, scale)
    bind(sw, knight)
    bpy.context.view_layer.update()
    after = sword_axes(sw)

    uv = normalize_uvs(sw)
    dropped = [a.name for a in sw.data.color_attributes]
    for a in list(sw.data.color_attributes):
        # An unused COLOR_0 multiplies into the skinning shader and renders the
        # prop black. See the project note on GLB vertex colours.
        sw.data.color_attributes.remove(a)


    # Verify the hold survived, in the units that make it meaningful.
    #
    # NOT "the wrist sits on the grip axis" -- it does not, and should not. The
    # RightHand joint is the WRIST; the sword is held in the palm, a hand's
    # width further down. Maria's wrist measures 0.080 m off her sword's grip
    # axis, which is 0.83 hand-lengths -- the fist closed around the hilt.
    #
    # What has to hold is that the SAME hold survives the move, so both figures
    # are expressed in hand-bone lengths and compared. That is the invariant the
    # transfer promises: the offset itself scales with the character, and only
    # its ratio to the hand is meant to be conserved.
    def hold(arm, axes):
        h = (arm.matrix_world @ arm.data.bones[HAND_BONE].matrix_local).to_translation()
        t = float((Vector(h) - axes["centre"]).dot(axes["axis"]))
        perp = ((Vector(h) - axes["centre"]) - axes["axis"] * t).length
        lo, hi = sorted((axes["grip_end_t"], axes["guard_t"]))
        return {"along_grip": (t - lo) / max(hi - lo, 1e-9),
                "perp_in_hands": perp / _bone_length(arm, HAND_BONE),
                "on_grip": lo - 1e-3 <= t <= hi + 1e-3}

    was, now = hold(maria_rest, before), hold(knight, after)
    if not now["on_grip"]:
        raise RuntimeError("the wrist projects to %.3f along the grip, off its "
                           "span" % now["along_grip"])
    drift = max(abs(was["along_grip"] - now["along_grip"]),
                abs(was["perp_in_hands"] - now["perp_in_hands"]))
    if drift > 1e-3:
        raise RuntimeError("the hold moved in the fist by %.5f; the transfer is "
                           "not a change of basis" % drift)

    removed = strip_maria(maria)
    sw.name = SWORD_OUT
    sw.data.name = SWORD_OUT

    report = {
        "scale_bone": SCALE_BONE,
        "scale": round(scale, 5),
        "rest_frames_apart_deg": round(float(rest_angle), 4),
        "length_before_m": round(before["length"], 4),
        "length_after_m": round(after["length"], 4),
        "grip_m": round(after["grip_length"], 4),
        "blade_m": round(after["blade_length"], 4),
        "guard_width_m": round(after["guard_r"] * 2.0, 4),
        "wrist_along_grip": round(now["along_grip"], 4),
        "wrist_off_grip_in_hand_lengths": round(now["perp_in_hands"], 4),
        "hold_drift": round(drift, 8),
        "uv": uv,
        "colour_attrs_dropped": dropped,
        "armature_removed": removed,
        "verts": len(sw.data.vertices),
    }
    print("ADD_MINIBOSS_SWORD_RESULT " + repr(report))
    return report


if __name__ == "__main__":
    main()
    if "--save" in sys.argv:
        bpy.ops.wm.save_mainfile()
        print("SAVED " + bpy.data.filepath)
