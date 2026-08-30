"""Retarget the Blood Knight (miniboss) model onto the 68-clip Mixamo rig in pack.blend.

Same problem and same fix as tools/retarget_sekiro.py: the model ships on its
own 64-bone rig (`hips`, `L_shoulder`, ...) in a relaxed rest pose, the clips
in this file live on 69-bone Mixamo rigs (`mixamorig:*`) in a T-pose, and
tools/merge_animations.py needs ONE armature carrying both the skin and every
clip. The move from model-rig to Mixamo-rig is safe for the same reason it was
for Sekiro: the clips are rotation-only (plus Hips translation), so they
encode joint ANGLES, and the rest skeleton can be rebuilt onto the model's own
joints without changing what any clip means.

Where this model differs from Sekiro, and why the pipeline is shorter:
  * ONE mesh, not 60 rigid shells -- so there is no garment/body decoupling
    (phase0, phase5) and no coincident-surface z-fighting between shells
    (phase4b) to fix up.
  * No props (no sword, no katana) yet -- phase4a is skipped. Binding a
    weapon later follows the same PROPS-table approach retarget_sekiro.py
    uses, once one exists for this character.
  * The elbow-plate vertices (couters) that sit far from the elbow bone's own
    axis are rigidly single-bone weighted (weight 1.0) armor bulk, not weight
    bled in from a garment far away like Sekiro's coat skirt -- measured
    before writing this, see the retarget session notes. Tapering them the
    way phase0 tapers Sekiro's coat would incorrectly strip real elbow-armor
    weighting, so that phase is not needed here at all.

Bone mapping note: like Sekiro's model, this rig has one bone (`spine`)
between hips and chest where Mixamo has three (Spine, Spine1, Spine2). `spine`
maps to `mixamorig:Spine1`, `chest` (which the shoulders parent off) maps to
`mixamorig:Spine2`, and `mixamorig:Spine` is left unmapped -- phase2 already
carries it generically, interpolated along the rebuilt Hips->Spine1 segment.

Alignment target, and why it is NOT bone.tail: retarget_sekiro.py swings each
bone onto `(pb.tail - pb.head)`, which works there because that rig's tail
always points at the next joint down the chain. Measured on THIS rig: hips,
spine, chest, neck and head all point their tail within a few degrees of pure
+Y -- a fixed reference/twist axis, not "toward my child" -- and the arms and
legs are similarly off. Swinging hips onto Mixamo's Hips direction (which
IS "up, toward Spine") on that basis rotates the whole character, since hips
is root and the rotation cascades to every descendant; spine, chest etc. each
being independently "corrected" the same wrong way folds the body over
further instead of fixing it.

The fix: for any model bone with exactly one mapped child, phase1 aligns it
using `(child.head - self.head)` instead of its own tail -- i.e. it rebuilds
the "tail toward child" direction retarget_sekiro.py could take for granted,
using the model's own joint positions instead of trusting this rig's tail
convention. The matching Mixamo target direction is rebuilt the same way
(child bone's head minus this bone's head), so both sides of the comparison
use the same convention. A bone with zero mapped children (finger tips, toes,
head's `top`) or more than one (`hips`: spine + two legs; `wrist`: five
fingers) has no unambiguous "toward child" direction and is left unrotated --
CHAIN_CHILD_OVERRIDE picks the anatomically-continuing child for the two
ambiguous-but-resolvable cases (`chest`->neck over the shoulders, `wrist`->
middle finger, matching retarget_sekiro.py's own precedent of using the
middle finger as the hand's representative direction). Everything left
unrotated still gets positioned correctly by its parent's rotation through
ordinary FK; only ITS OWN further correction is skipped, which for a fingertip
or toe is not visible.

Run against the LIVE session (pack_miniboss.blend, opened from a copy of
pack.blend with `mesh` / `BloodKnightArmature` appended from miniboss.blend):
    exec(open("tools/retarget_miniboss.py").read())
    report = main()
"""

import json

import bpy
from mathutils import Matrix

MODEL_ARM = "BloodKnightArmature"
MAIN_ARM = "Armature"

# The factor the model itself was scaled by before retargeting (see the
# module docstring: 1.5x the stock Paladin placeholder's height). Bone-length
# changes from phase2 already scale rotation-driven limb swing for free -- a
# longer thigh bone swept through the same clip-authored ANGLE covers more
# ground. Hips' `.location` curves do not get that for free: they encode the
# mocap actor's absolute stride distance in armature-space units, unrelated
# to any particular character's bone lengths, so phase2b below rescales them
# by the same factor explicitly. Skipping this leaves a giant taking a
# human's stride length -- correct-looking limb articulation, visibly wrong
# ground speed.
SCALE_FACTOR = 1.4266482879517355
HIPS_BONE = "mixamorig:Hips"

# See the module docstring: which child continues the chain, for the two
# joints where more than one mapped child makes that ambiguous.
CHAIN_CHILD_OVERRIDE = {
    "chest": "neck",
    "L_wrist": "L_middle",
    "R_wrist": "R_middle",
}


def _chain_children(mapping):
    """model bone -> its direct children that are also mapped."""
    model = bpy.data.objects[MODEL_ARM]
    kids = {name: [] for name in mapping}
    for bone in model.data.bones:
        if bone.parent is not None and bone.parent.name in mapping and bone.name in mapping:
            kids[bone.parent.name].append(bone.name)
    return kids


def _chain_child(name, kids):
    """The single child to align `name` toward, or None if ambiguous/leaf."""
    if name in CHAIN_CHILD_OVERRIDE:
        return CHAIN_CHILD_OVERRIDE[name]
    children = kids.get(name, [])
    return children[0] if len(children) == 1 else None


def bone_map():
    """Model bone -> Mixamo bone. Every one of the model's 51 weighted bones maps."""
    m = {
        "hips": "mixamorig:Hips",
        "spine": "mixamorig:Spine1",
        "chest": "mixamorig:Spine2",
        "neck": "mixamorig:Neck",
        "head": "mixamorig:Head",
        "top": "mixamorig:HeadTop_End",
    }
    for src, dst in (("shoulder", "Shoulder"), ("arm", "Arm"),
                     ("elbow", "ForeArm"), ("wrist", "Hand")):
        for prefix, side in (("L", "Left"), ("R", "Right")):
            m["%s_%s" % (prefix, src)] = "mixamorig:%s%s" % (side, dst)
    for src, dst in (("thumb", "Thumb"), ("point", "Index"),
                     ("middle", "Middle"), ("ring", "Ring"), ("pink", "Pinky")):
        for prefix, side in (("L", "Left"), ("R", "Right")):
            m["%s_%s" % (prefix, src)] = "mixamorig:%sHand%s1" % (side, dst)
            for num in (2, 3, 4):
                m["%s_%s_%d" % (prefix, src, num)] = \
                    "mixamorig:%sHand%s%d" % (side, dst, num)
    for prefix, side in (("L", "Left"), ("R", "Right")):
        m["%s_leg" % prefix] = "mixamorig:%sUpLeg" % side
        m["%s_knee" % prefix] = "mixamorig:%sLeg" % side
        m["%s_ankle" % prefix] = "mixamorig:%sFoot" % side
        m["%s_foot" % prefix] = "mixamorig:%sToeBase" % side
        m["%s_toes" % prefix] = "mixamorig:%sToe_End" % side
    return m


def _activate(obj, mode="OBJECT"):
    active = bpy.context.view_layer.objects.active
    if active is not None and active.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")
    for o in bpy.context.view_layer.objects:
        if o.select_get():
            o.select_set(False)
    obj.hide_viewport = False
    obj.hide_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    if mode != "OBJECT":
        bpy.ops.object.mode_set(mode=mode)


def _hierarchy(armature):
    order = []

    def walk(bone):
        order.append(bone)
        for child in bone.children:
            walk(child)

    for bone in armature.data.bones:
        if bone.parent is None:
            walk(bone)
    return order


def _skinned_meshes(armature):
    return [o for o in bpy.data.objects if o.type == "MESH" and o.parent is armature]


# ---------------------------------------------------------------------------
# Phase 1 -- swing the model into the Mixamo rest directions
# ---------------------------------------------------------------------------
def phase1_align_to_mixamo_rest():
    model = bpy.data.objects[MODEL_ARM]
    main = bpy.data.objects[MAIN_ARM]
    mapping = bone_map()
    mw = main.matrix_world
    kids = _chain_children(mapping)

    target_heads = {bone.name: mw @ bone.head_local for bone in main.data.bones}

    def target_dir(name, chain_child):
        return (target_heads[mapping[chain_child]]
                - target_heads[mapping[name]]).normalized()

    _activate(model, "POSE")
    for pb in model.pose.bones:
        pb.matrix_basis = Matrix.Identity(4)
    bpy.context.view_layer.update()

    applied = []
    skipped = []
    for bone in _hierarchy(model):
        if bone.name not in mapping:
            continue
        chain_child = _chain_child(bone.name, kids)
        if chain_child is None:
            skipped.append(bone.name)
            continue
        pb = model.pose.bones[bone.name]
        pb_child = model.pose.bones[chain_child]
        cur = (pb_child.head - pb.head).normalized()
        tgt = target_dir(bone.name, chain_child)
        rot = cur.rotation_difference(tgt).to_matrix().to_4x4()
        head = pb.head.copy()
        pb.matrix = (Matrix.Translation(head) @ rot @ Matrix.Translation(-head)
                     @ pb.matrix)
        bpy.context.view_layer.update()
        applied.append({"bone": bone.name, "toward": chain_child,
                        "deg": round(cur.angle(tgt) * 57.29577951, 2)})

    worst = 0.0
    worst_bone = None
    for bone in _hierarchy(model):
        if bone.name not in mapping:
            continue
        chain_child = _chain_child(bone.name, kids)
        if chain_child is None:
            continue
        pb = model.pose.bones[bone.name]
        pb_child = model.pose.bones[chain_child]
        cur = (pb_child.head - pb.head).normalized()
        angle = cur.angle(target_dir(bone.name, chain_child)) * 57.29577951
        if angle > worst:
            worst, worst_bone = angle, bone.name

    bpy.ops.object.mode_set(mode="OBJECT")
    return {"rotated": len(applied), "excluded_no_chain_child": skipped,
            "max_residual_deg": round(worst, 4), "worst_bone": worst_bone,
            "largest_swings": sorted(applied, key=lambda r: -r["deg"])[:6]}


def phase1b_bake_pose_into_meshes():
    model = bpy.data.objects[MODEL_ARM]
    meshes = _skinned_meshes(model)

    baked = []
    for obj in meshes:
        if obj.data.users != 1:
            raise RuntimeError("%s shares mesh data; apply would leak" % obj.name)
        mods = [m for m in obj.modifiers if m.type == "ARMATURE"]
        if len(mods) != 1:
            raise RuntimeError("%s has %d armature modifiers" % (obj.name, len(mods)))
        _activate(obj)
        bpy.ops.object.modifier_apply(modifier=mods[0].name)
        baked.append(obj.name)

    _activate(model, "POSE")
    bpy.ops.pose.armature_apply()
    bpy.ops.object.mode_set(mode="OBJECT")

    for obj in meshes:
        mod = obj.modifiers.new(name="Armature", type="ARMATURE")
        mod.object = model
    return {"baked": baked, "n": len(baked)}


# ---------------------------------------------------------------------------
# Phase 2 -- rebuild the Mixamo rest skeleton onto the model's joints
# ---------------------------------------------------------------------------
def phase2_rebuild_rest():
    model = bpy.data.objects[MODEL_ARM]
    main = bpy.data.objects[MAIN_ARM]
    mapping = bone_map()
    inverse = {v: k for k, v in mapping.items()}
    mw_inv = main.matrix_world.inverted()
    scale = main.matrix_world.to_scale().x

    src = {b.name: (b.head_local.copy(), b.tail_local.copy(), b.length)
           for b in model.data.bones}
    old_head = {b.name: b.head_local.copy() for b in main.data.bones}

    _activate(main, "EDIT")
    ebs = main.data.edit_bones

    connected = [eb.name for eb in ebs if eb.use_connect]
    for eb in ebs:
        eb.use_connect = False

    moved = {}
    for eb in ebs:
        model_bone = inverse.get(eb.name)
        if model_bone is None:
            continue
        direction = (eb.tail - eb.head).normalized()
        roll = eb.roll
        head_w, _, length_w = src[model_bone]
        new_head = mw_inv @ head_w
        eb.head = new_head
        eb.tail = new_head + direction * (length_w / scale)
        eb.roll = roll
        moved[eb.name] = new_head.copy()

    spine = ebs.get("mixamorig:Spine")
    if spine is not None:
        h0, h1 = old_head["mixamorig:Hips"], old_head["mixamorig:Spine1"]
        frac = (old_head["mixamorig:Spine"] - h0).length / max((h1 - h0).length, 1e-9)
        n0, n1 = moved["mixamorig:Hips"], moved["mixamorig:Spine1"]
        stretch = (n1 - n0).length / max((h1 - h0).length, 1e-9)
        direction = (spine.tail - spine.head).normalized()
        roll, length = spine.roll, spine.length
        spine.head = n0 + (n1 - n0) * frac
        spine.tail = spine.head + direction * length * stretch
        spine.roll = roll
        moved["mixamorig:Spine"] = spine.head.copy()

    carried = []
    for eb in ebs:
        if eb.name in moved:
            continue
        anc = eb.parent
        while anc is not None and anc.name not in moved:
            anc = anc.parent
        if anc is None:
            continue
        shift = moved[anc.name] - old_head[anc.name]
        roll = eb.roll
        eb.head = eb.head + shift
        eb.tail = eb.tail + shift
        eb.roll = roll
        carried.append(eb.name)

    bpy.ops.object.mode_set(mode="OBJECT")

    worst = 0.0
    worst_bone = None
    for bone in main.data.bones:
        before = _ORIENT_BEFORE.get(bone.name)
        if before is None:
            continue
        delta = (before.inverted() @ bone.matrix_local.to_3x3().normalized())
        angle = abs(delta.to_quaternion().angle) * 57.29577951
        if angle > worst:
            worst, worst_bone = angle, bone.name
    return {"moved": len(moved), "carried": len(carried),
            "disconnected": len(connected),
            "max_orientation_drift_deg": round(worst, 6), "worst_bone": worst_bone}


_ORIENT_BEFORE = {}


def snapshot_orientations():
    main = bpy.data.objects[MAIN_ARM]
    _ORIENT_BEFORE.clear()
    for bone in main.data.bones:
        _ORIENT_BEFORE[bone.name] = bone.matrix_local.to_3x3().normalized()
    return {"snapshot": len(_ORIENT_BEFORE)}


# ---------------------------------------------------------------------------
# Phase 3 -- move the skin onto the Mixamo rig
# ---------------------------------------------------------------------------
MAX_INFLUENCES = 4


def phase3_rebind():
    model = bpy.data.objects[MODEL_ARM]
    main = bpy.data.objects[MAIN_ARM]
    mapping = bone_map()
    meshes = _skinned_meshes(model)

    renamed = 0
    for obj in meshes:
        for vg in obj.vertex_groups:
            if vg.name in mapping:
                vg.name = mapping[vg.name]
                renamed += 1

    report = []
    for obj in meshes:
        _activate(obj)
        bpy.ops.object.vertex_group_limit_total(group_select_mode="ALL",
                                                limit=MAX_INFLUENCES)
        bpy.ops.object.vertex_group_normalize_all(group_select_mode="ALL",
                                                  lock_active=False)
        worst = 0
        bad = 0
        for v in obj.data.vertices:
            n = sum(1 for g in v.groups if g.weight > 1e-6)
            total = sum(g.weight for g in v.groups if g.weight > 1e-6)
            worst = max(worst, n)
            if abs(total - 1.0) > 1e-3:
                bad += 1
        report.append({"mesh": obj.name, "max_influences": worst, "unnormalized": bad})

    for obj in meshes:
        for mod in obj.modifiers:
            if mod.type == "ARMATURE":
                mod.object = main
        world = obj.matrix_world.copy()
        obj.parent = main
        obj.matrix_parent_inverse = main.matrix_world.inverted()
        obj.matrix_world = world
    return {"vertex_groups_renamed": renamed, "meshes": report}


# ---------------------------------------------------------------------------
# Phase 4 -- hygiene
# ---------------------------------------------------------------------------
def phase4_strip_and_prune():
    """Drop colour attributes and the Paladin, and remove the now-empty model rig.

    Colour attributes no material reads still export as COLOR_0, and this
    game's skinning shader multiplies it into finalColor -- a black or
    zero-alpha attribute renders the model black. See
    assets/shaders/glsl330/skinning.fs.
    """
    main = bpy.data.objects[MAIN_ARM]
    model = bpy.data.objects.get(MODEL_ARM)

    stripped = []
    for obj in bpy.data.objects:
        if obj.type != "MESH":
            continue
        for attr in list(obj.data.color_attributes):
            stripped.append("%s.%s" % (obj.name, attr.name))
            obj.data.color_attributes.remove(attr)

    removed = []
    for obj in list(bpy.data.objects):
        if obj.type == "MESH" and obj.name.startswith("Paladin"):
            removed.append(obj.name)
            bpy.data.objects.remove(obj, do_unlink=True)
    if model is not None:
        removed.append(model.name)
        bpy.data.objects.remove(model, do_unlink=True)

    kept = [o.name for o in bpy.data.objects if o.type == "MESH" and o.parent is main]
    return {"color_attrs_stripped": stripped, "removed": removed, "skin": kept}


# ---------------------------------------------------------------------------
# Phase 2b -- scale every clip's Hips translation to match the character
# ---------------------------------------------------------------------------
def phase2b_rescale_hips_translation():
    """Multiply every clip's Hips `.location` keyframes by SCALE_FACTOR.

    Runs against the SAME per-clip action data merge_animations.py will later
    read off the other 60 armature objects (Armature.001..060) -- this has to
    happen before that script's own `_rescale_translation_keys`, which applies
    a DIFFERENT factor (the exported object's 0.01 cm->m scale) on top; the
    two compose by simple multiplication regardless of order, so doing this
    now, once, here, is equivalent to teaching the shared merge script about
    a per-character scale it has no business knowing about.

    Uses the same layered-action / channelbag walk merge_animations.py's own
    `_rescale_translation_keys` uses (Blender's 4.4+ Animation rework), so
    this is the one place a future Blender version's fcurve API change would
    need updating in both scripts together.
    """
    main = bpy.data.objects[MAIN_ARM]
    path = 'pose.bones["%s"].location' % HIPS_BONE

    scaled_actions = []
    for obj in bpy.data.objects:
        if obj.type != "ARMATURE" or obj is main:
            continue
        anim = obj.animation_data
        if not (anim and anim.action):
            continue
        action = anim.action
        if action.name in scaled_actions:
            continue
        curves = 0
        for layer in action.layers:
            for strip in layer.strips:
                for bag in strip.channelbags:
                    for fcurve in bag.fcurves:
                        if fcurve.data_path != path:
                            continue
                        for key in fcurve.keyframe_points:
                            key.co.y *= SCALE_FACTOR
                            key.handle_left.y *= SCALE_FACTOR
                            key.handle_right.y *= SCALE_FACTOR
                        fcurve.update()
                        curves += 1
        if curves:
            scaled_actions.append(action.name)
    return {"scale_factor": SCALE_FACTOR, "actions_scaled": len(scaled_actions)}


def main():
    report = {}
    report["orientations"] = snapshot_orientations()
    report["phase1"] = phase1_align_to_mixamo_rest()
    report["phase1b"] = phase1b_bake_pose_into_meshes()
    report["phase2"] = phase2_rebuild_rest()
    report["phase2b"] = phase2b_rescale_hips_translation()
    report["phase3"] = phase3_rebind()
    report["phase4"] = phase4_strip_and_prune()
    print("RETARGET_RESULT " + json.dumps(report))
    return report
