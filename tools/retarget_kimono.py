"""Move the kimono character off its 105-bone Biped rig onto the Mixamo rig.

    blender -b ~/Documents/3D/Model/pack.blend \
        --python tools/retarget_kimono.py -- --save

Reads ~/Documents/3D/Model/kimono.blend (built by tools/make_kimono_source.py),
appends it into pack.blend's Mixamo rig and clip library, and writes
~/Documents/3D/Model/pack_kimono.blend. Step 2 of tools/rebuild_kimono.sh.

Same problem and same five phases as tools/retarget_sekiro.py and
tools/retarget_miniboss.py: the model ships on its own rig, the clips live on
Mixamo rigs, and tools/merge_animations.py needs ONE armature carrying both the
skin and every clip. The move is safe for the same reason it was there -- Mixamo
clips are rotation-only plus Hips translation, so they encode joint ANGLES and
the rest skeleton can be rebuilt onto this model's joints without changing what
any clip means.

THE SIDE NAMES ARE MIRRORED
---------------------------
This rig's `L_*` is Mixamo's `Right*`. Not a guess -- measured:

    both rigs face -Y      toes sit at lower Y than the ankle on both
    mixamorig:LeftFoot     x = +0.098
    L_Foot                 x = -0.074

Same facing, opposite sides. Mapping `L_` to `Left` would build a mirrored
character: feet crossed, every clip played back reversed, and the sword in the
wrong fist. Nothing about that reads as a mapping error once it is in the game
-- it reads as bad animation -- so it is asserted in `verify_sides()` below
rather than left to the reader of the table.

Which also decides which blade survives, upstream in make_kimono_source.py:
Mixamo's greatsword clips grip with `mixamorig:RightHand`, which is this rig's
`L_Hand`, so `L_Katana` is kept and `R_Katana` dropped.

FORTY-SEVEN BONES MIXAMO DOES NOT HAVE
--------------------------------------
Both earlier retargets had models whose every weighted bone mapped. This one
does not, and the difference is not small: of 105 bones, 56 map and the rest are
twist bones, garment chains and the sword. They carry real weight --

    the hakama (5,867 verts) is weighted almost entirely to `*_Mobakama_*`
    the haori panels hang off `*_Haori_Front/Back_*`
    the blade is 100% `L_Katana`

-- so dropping them drops the skirt, the coat and the sword. `phase2c` rebuilds
each one onto the Mixamo armature at its own rest position, parented to the
Mixamo counterpart of whichever mapped bone it anchors at (every extra anchors
at one; that was checked, not assumed).

Which extras get carried is COMPUTED, not listed: any unmapped bone that carries
weight, plus any unmapped ancestor needed to reach a mapped one. That is what
keeps helper bones nothing is weighted to (`Master`, `Root`, the knee helpers)
from costing joints, and it is why this file has no table to fall out of date.

Nothing animates them. The clips touch Mixamo bones only, so a carried bone sits
at rest and follows its parent through FK -- the hakama swings rigidly with the
legs and has no secondary motion, exactly as the miniboss's greatsword is one
rigid bone off his hand.

Bone budget: 69 Mixamo + 47 carried = 116, against MAX_BONE_NUM 128 in
assets/shaders/glsl330/skinning.vs. `phase2c` refuses to exceed it, because
overflowing that array is a shader that reads garbage rather than an error.
"""

import json
import os
import sys

import bpy
from mathutils import Matrix, Vector

SRC_BLEND = os.path.expanduser("~/Documents/3D/Model/kimono.blend")
OUT = os.path.expanduser("~/Documents/3D/Model/pack_kimono.blend")

MODEL_ARM = "KimonoArmature"
MAIN_ARM = "Armature"
HIPS_BONE = "mixamorig:Hips"

# assets/shaders/glsl330/skinning.vs. Overflowing it is silent.
MAX_BONE_NUM = 128

# Which child continues the chain, where more than one mapped child makes that
# ambiguous. Same two cases the other two retargets have, plus the spine fork.
CHAIN_CHILD_OVERRIDE = {
    "Spine2": "Neck",
    "L_Hand": "L_Finger2",
    "R_Hand": "R_Finger2",
}

# Set by measure_scale_factor(), consumed by phase2b. Measured rather than
# hardcoded: this model is a different height from the clip rig and the number
# is a property of the pair, not of either one.
_SCALE_FACTOR = None
_ORIENT_BEFORE = {}


def bone_map():
    """Kimono bone -> Mixamo bone. L_ is Mixamo's Right; see the docstring."""
    m = {
        "Pelvis": "mixamorig:Hips",
        "Spine": "mixamorig:Spine",
        "Spine1": "mixamorig:Spine1",
        "Spine2": "mixamorig:Spine2",
        "Neck": "mixamorig:Neck",
        "Head": "mixamorig:Head",
    }
    # THE SWAP. `L_` on this rig is the -X side, which is Mixamo's Right.
    for prefix, side in (("L", "Right"), ("R", "Left")):
        m["%s_Clavicle" % prefix] = "mixamorig:%sShoulder" % side
        m["%s_UpperArm" % prefix] = "mixamorig:%sArm" % side
        m["%s_Forearm" % prefix] = "mixamorig:%sForeArm" % side
        m["%s_Hand" % prefix] = "mixamorig:%sHand" % side
        m["%s_Thigh" % prefix] = "mixamorig:%sUpLeg" % side
        # `*_Knee` is a helper bone sharing L_Thigh as parent with `*_Calf`;
        # `*_Calf` is the shin, because it is the one the foot hangs off.
        m["%s_Calf" % prefix] = "mixamorig:%sLeg" % side
        m["%s_Foot" % prefix] = "mixamorig:%sFoot" % side
        m["%s_Toe0" % prefix] = "mixamorig:%sToeBase" % side
        # Finger0 is the thumb, then index/middle/ring/pinky; the second and
        # third joints carry the digit number twice (L_Finger21 = middle 2).
        for digit, name in enumerate(("Thumb", "Index", "Middle", "Ring", "Pinky")):
            m["%s_Finger%d" % (prefix, digit)] = \
                "mixamorig:%sHand%s1" % (side, name)
            for joint in (1, 2):
                m["%s_Finger%d%d" % (prefix, digit, joint)] = \
                    "mixamorig:%sHand%s%d" % (side, name, joint + 1)
    return m


def verify_sides():
    """Refuse to run if the mirror this whole mapping rests on is not there.

    Cheap, and the failure it catches is one that looks like bad animation
    rather than like a bad table.
    """
    model = bpy.data.objects[MODEL_ARM]
    main = bpy.data.objects[MAIN_ARM]

    def side_x(arm, bone):
        return (arm.matrix_world @ arm.data.bones[bone].head_local).x

    def faces_minus_y(arm, ankle, toe):
        a = arm.matrix_world @ arm.data.bones[ankle].head_local
        t = arm.matrix_world @ arm.data.bones[toe].head_local
        return t.y < a.y

    facing = (faces_minus_y(main, "mixamorig:LeftFoot", "mixamorig:LeftToeBase"),
              faces_minus_y(model, "L_Foot", "L_Toe0"))
    if facing != (True, True):
        raise RuntimeError(
            "one of the rigs does not face -Y (mixamo=%s kimono=%s). The side "
            "mapping is derived from a shared facing; re-measure before "
            "trusting it." % facing)

    mixamo_left = side_x(main, "mixamorig:LeftFoot")
    kimono_l = side_x(model, "L_Foot")
    if not (mixamo_left > 0 and kimono_l < 0):
        raise RuntimeError(
            "expected mirrored sides (mixamo LeftFoot x=%+.3f, kimono L_Foot "
            "x=%+.3f). bone_map()'s L->Right swap is wrong for this file."
            % (mixamo_left, kimono_l))
    return {"mixamo_LeftFoot_x": round(mixamo_left, 4),
            "kimono_L_Foot_x": round(kimono_l, 4), "mirrored": True}


def append_model():
    """Bring kimono.blend's objects into this file."""
    if bpy.data.objects.get(MODEL_ARM):
        return {"appended": "already present"}
    with bpy.data.libraries.load(SRC_BLEND) as (src, dst):
        dst.objects = list(src.objects)
    names = []
    for obj in dst.objects:
        if obj is None:
            continue
        bpy.context.scene.collection.objects.link(obj)
        names.append(obj.name)
    if not bpy.data.objects.get(MODEL_ARM):
        raise RuntimeError("kimono.blend did not provide %r" % MODEL_ARM)
    return {"appended": names}


def measure_scale_factor():
    """Hip height ratio, model over clip rig.

    Every clip's Hips `.location` curve is the mocap actor's absolute travel in
    the CLIP rig's units. A shorter character replaying those numbers takes a
    taller character's stride: correct-looking articulation, visibly wrong
    ground speed. phase2b multiplies them by this.
    """
    global _SCALE_FACTOR
    model = bpy.data.objects[MODEL_ARM]
    main = bpy.data.objects[MAIN_ARM]
    mine = (model.matrix_world @ model.data.bones["Pelvis"].head_local).z
    theirs = (main.matrix_world @ main.data.bones[HIPS_BONE].head_local).z
    _SCALE_FACTOR = mine / theirs
    return {"kimono_hip_z": round(mine, 4), "mixamo_hip_z": round(theirs, 4),
            "scale_factor": round(_SCALE_FACTOR, 6)}


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


def _chain_children(mapping):
    model = bpy.data.objects[MODEL_ARM]
    kids = {name: [] for name in mapping}
    for bone in model.data.bones:
        if bone.parent is not None and bone.parent.name in mapping and bone.name in mapping:
            kids[bone.parent.name].append(bone.name)
    return kids


def _chain_child(name, kids):
    if name in CHAIN_CHILD_OVERRIDE:
        return CHAIN_CHILD_OVERRIDE[name]
    children = kids.get(name, [])
    return children[0] if len(children) == 1 else None


def snapshot_orientations():
    main = bpy.data.objects[MAIN_ARM]
    _ORIENT_BEFORE.clear()
    for bone in main.data.bones:
        _ORIENT_BEFORE[bone.name] = bone.matrix_local.to_3x3().normalized()
    return {"snapshot": len(_ORIENT_BEFORE)}


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

    applied, skipped = [], []
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

    worst, worst_bone = 0.0, None
    for bone in _hierarchy(model):
        if bone.name not in mapping:
            continue
        chain_child = _chain_child(bone.name, kids)
        if chain_child is None:
            continue
        pb = model.pose.bones[bone.name]
        cur = (model.pose.bones[chain_child].head - pb.head).normalized()
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

    src = {b.name: (b.head_local.copy(), b.length) for b in model.data.bones}
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
        head_w, length_w = src[model_bone]
        new_head = mw_inv @ (model.matrix_world @ head_w)
        eb.head = new_head
        eb.tail = new_head + direction * (length_w / scale)
        eb.roll = roll
        moved[eb.name] = new_head.copy()

    # Anything Mixamo has that this model does not -- HeadTop_End, the eyes,
    # the toe ends -- rides along on its nearest rebuilt ancestor's shift.
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

    worst, worst_bone = 0.0, None
    for bone in main.data.bones:
        before = _ORIENT_BEFORE.get(bone.name)
        if before is None:
            continue
        delta = before.inverted() @ bone.matrix_local.to_3x3().normalized()
        angle = abs(delta.to_quaternion().angle) * 57.29577951
        if angle > worst:
            worst, worst_bone = angle, bone.name
    return {"moved": len(moved), "carried": len(carried),
            "disconnected": len(connected),
            "max_orientation_drift_deg": round(worst, 6), "worst_bone": worst_bone}


# ---------------------------------------------------------------------------
# Phase 2c -- carry the model's own extra bones onto the Mixamo rig
# ---------------------------------------------------------------------------
def _weighted_bone_names(model):
    """Vertex groups that actually move something, across every skinned mesh."""
    used = set()
    for obj in _skinned_meshes(model):
        names = {i: vg.name for i, vg in enumerate(obj.vertex_groups)}
        for v in obj.data.vertices:
            for g in v.groups:
                if g.weight > 1e-4:
                    used.add(names.get(g.group))
    used.discard(None)
    return used


def phase2c_carry_extras():
    model = bpy.data.objects[MODEL_ARM]
    main = bpy.data.objects[MAIN_ARM]
    mapping = bone_map()
    weighted = _weighted_bone_names(model)

    # An extra is any unmapped bone that carries weight, plus any unmapped
    # ancestor between it and the mapped bone it hangs off -- without those the
    # chain has a hole and the child cannot be parented.
    needed = set()
    for bone in model.data.bones:
        if bone.name in mapping or bone.name not in weighted:
            continue
        node = bone
        while node is not None and node.name not in mapping:
            needed.add(node.name)
            node = node.parent
        if node is None:
            raise RuntimeError("%s anchors at no mapped bone" % bone.name)

    order = [b for b in _hierarchy(model) if b.name in needed]
    total = len(main.data.bones) + len(order)
    if total > MAX_BONE_NUM:
        raise RuntimeError(
            "%d bones after carrying %d extras, over MAX_BONE_NUM %d in "
            "assets/shaders/glsl330/skinning.vs. Overflowing that uniform "
            "array reads garbage rather than failing, so this stops here."
            % (total, len(order), MAX_BONE_NUM))

    model_mw = model.matrix_world
    mw_inv = main.matrix_world.inverted()
    rot_inv = mw_inv.to_3x3()

    _activate(main, "EDIT")
    ebs = main.data.edit_bones
    made = []
    for bone in order:
        if ebs.get(bone.name):
            raise RuntimeError("%r already exists on the Mixamo rig" % bone.name)
        eb = ebs.new(bone.name)
        eb.head = mw_inv @ (model_mw @ bone.head_local)
        eb.tail = mw_inv @ (model_mw @ bone.tail_local)
        # Roll matters: the mesh was bound against this bone's rest frame, and
        # phase3 rebinds by name without touching weights. Head and tail alone
        # leave the roll at whatever new() defaulted to, which twists the skin
        # around the bone axis.
        z_world = (model_mw.to_3x3() @ bone.matrix_local.to_3x3()
                   @ Vector((0.0, 0.0, 1.0)))
        eb.align_roll((rot_inv @ z_world).normalized())

        parent = bone.parent
        while parent is not None and parent.name not in needed \
                and parent.name not in mapping:
            parent = parent.parent
        if parent is None:
            raise RuntimeError("%s lost its anchor" % bone.name)
        eb.parent = ebs[mapping.get(parent.name, parent.name)]
        made.append(bone.name)

    bpy.ops.object.mode_set(mode="OBJECT")
    return {"carried": len(made), "bones_total": len(main.data.bones),
            "budget": MAX_BONE_NUM, "names": made}


# ---------------------------------------------------------------------------
# Phase 2b -- scale every clip's Hips translation to this character
# ---------------------------------------------------------------------------
def phase2b_rescale_hips_translation():
    main = bpy.data.objects[MAIN_ARM]
    path = 'pose.bones["%s"].location' % HIPS_BONE
    scaled = []
    for obj in bpy.data.objects:
        if obj.type != "ARMATURE" or obj is main:
            continue
        anim = obj.animation_data
        if not (anim and anim.action) or anim.action.name in scaled:
            continue
        curves = 0
        for layer in anim.action.layers:
            for strip in layer.strips:
                for bag in strip.channelbags:
                    for fcurve in bag.fcurves:
                        if fcurve.data_path != path:
                            continue
                        for key in fcurve.keyframe_points:
                            key.co.y *= _SCALE_FACTOR
                            key.handle_left.y *= _SCALE_FACTOR
                            key.handle_right.y *= _SCALE_FACTOR
                        fcurve.update()
                        curves += 1
        if curves:
            scaled.append(anim.action.name)
    return {"scale_factor": round(_SCALE_FACTOR, 6), "actions_scaled": len(scaled)}


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

    # Anything still pointing at a bone the main rig does not have would be
    # weight thrown on the floor -- silently, as a limp piece of cloth.
    bones = {b.name for b in main.data.bones}
    orphaned = sorted({vg.name for obj in meshes for vg in obj.vertex_groups}
                      - bones)
    if orphaned:
        raise RuntimeError("vertex groups with no bone after rebind: %s"
                           % orphaned)

    # REBIND BEFORE NORMALISING. Both operators below act on `deform` groups
    # only -- the ones naming a bone of the armature the mesh is currently bound
    # to -- and until this loop runs that is still the model rig. Renamed groups
    # would not count and CARRIED groups, which keep their names, would: the
    # operator then normalises each vertex across the extras alone and pushes a
    # lone twist bone from its real 0.12 up to 1.0, on top of a full-weight
    # Mixamo group it never saw. Weights summing to 1.9.
    #
    # Neither earlier retarget could hit this. Every weighted bone mapped there,
    # so after renaming NOTHING was a deform group and the pass was a harmless
    # no-op over already-normalised weights. Carrying bones across is what makes
    # the order matter.
    for obj in meshes:
        for mod in obj.modifiers:
            if mod.type == "ARMATURE":
                mod.object = main
        world = obj.matrix_world.copy()
        obj.parent = main
        obj.matrix_parent_inverse = main.matrix_world.inverted()
        obj.matrix_world = world

    report = []
    for obj in meshes:
        _activate(obj)
        bpy.ops.object.vertex_group_limit_total(group_select_mode="ALL",
                                                limit=MAX_INFLUENCES)
        bpy.ops.object.vertex_group_normalize_all(group_select_mode="ALL",
                                                  lock_active=False)
        worst = bad = zero = 0
        for v in obj.data.vertices:
            weights = [g.weight for g in v.groups if g.weight > 1e-6]
            worst = max(worst, len(weights))
            total = sum(weights)
            if total <= 1e-6:
                zero += 1
            elif abs(total - 1.0) > 1e-3:
                bad += 1
        report.append({"mesh": obj.name, "max_influences": worst,
                       "unnormalized": bad, "zero_weight": zero})

    # Asserted, not reported. A vertex whose weights do not sum to 1 is dragged
    # toward the origin by the shortfall, and one with no weight at all lands on
    # it -- both of which read as a modelling fault rather than as a rebind that
    # ran in the wrong order.
    broken = [r for r in report if r["unnormalized"] or r["zero_weight"]]
    if broken:
        raise RuntimeError("weights not clean after rebind: %s" % broken)
    return {"vertex_groups_renamed": renamed, "meshes": report}


# ---------------------------------------------------------------------------
# Phase 4 -- hygiene
# ---------------------------------------------------------------------------
def phase4_strip_and_prune():
    """Drop colour attributes, the Paladin, and the now-empty model rig.

    Colour attributes no material reads still export as COLOR_0, and this
    game's skinning shader multiplies it into finalColor -- a black or
    zero-alpha attribute renders the model black.
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


def main():
    report = {}
    report["append"] = append_model()
    report["sides"] = verify_sides()
    report["scale"] = measure_scale_factor()
    report["orientations"] = snapshot_orientations()
    report["phase1"] = phase1_align_to_mixamo_rest()
    report["phase1b"] = phase1b_bake_pose_into_meshes()
    report["phase2"] = phase2_rebuild_rest()
    report["phase2c"] = phase2c_carry_extras()
    report["phase2b"] = phase2b_rescale_hips_translation()
    report["phase3"] = phase3_rebind()
    report["phase4"] = phase4_strip_and_prune()
    print("RETARGET_RESULT " + json.dumps(report))

    if "--save" in sys.argv:
        bpy.ops.wm.save_as_mainfile(filepath=OUT)
        print("SAVED " + OUT)
    return report


if __name__ == "__main__":
    main()
