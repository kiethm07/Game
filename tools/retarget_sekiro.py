"""Retarget the LowPolySekiroRigged model onto the 60-clip Mixamo rig in pack.blend.

The model ships on its own 40-bone Blender-style rig (`Hips`, `shoulder.L`, ...)
in a relaxed A-pose. The 68 actions in this file all live on 69-bone Mixamo rigs
(`mixamorig:*`) in a T-pose. `tools/merge_animations.py` needs ONE armature
carrying both the skin and every clip, so the model has to move onto the Mixamo
rig without disturbing the clips.

The move is safe because of one measured fact: across all 68 actions, every one
of the 10116 non-Hips `.location` f-curves is exactly zero and no `.scale` curve
leaves 1.0. The clips are pure rotation plus Hips translation, so they encode
joint ANGLES, not joint POSITIONS -- which means the rig's rest skeleton can be
rebuilt onto the model's own joints and every clip still plays correctly. That is
what Mixamo itself does when it retargets one clip library across characters of
different proportions.

Doing it the other way round -- binding the model to the stock Mixamo rest -- was
measured first and rejected: the model's joints land a median 7 cm (wrist 12 cm,
knee 5.7 cm) from the Mixamo joints, so every shell would pivot about a point
several centimetres outside itself and the kit would come apart at the joints.

Phases:
  1. Pose the model's own rig so each bone points along its Mixamo counterpart's
     rest direction, then bake that pose into the mesh data. Limb LENGTHS are
     untouched, so the artist's proportions survive; only the pose changes.
  2. Rebuild `Armature`'s rest skeleton onto the model's joints, preserving every
     bone's direction and roll exactly (orientation is what the clips are
     relative to; position is not).
  3. Rename the vertex groups to `mixamorig:*`, cap influences at 4, normalise,
     and rebind to `Armature`.
  4. Hygiene: bind the rigid props (see PROPS), break up coincident garment/body
     surfaces, strip colour attributes, drop the Paladin.

Run against the LIVE session (the model import is not in pack.blend on disk),
after tools/add_katana.py has seated the katana in the right fist:
    exec(open("tools/retarget_sekiro.py").read())
    report = main()
"""

import json

import bpy
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

SEKIRO_ARM = "SekiroCharacterLowPolyRigged"
MAIN_ARM = "Armature"
SWORD = " Sword.002"

# Rigid props: object name -> (model bone, Mixamo bone). A prop carries no
# armature modifier and no weights of its own, so every phase that walks the
# SKINNED meshes has to skip it. phase0 above all: it strips arm influence from
# any vertex more than ARM_CUT from an arm bone, and a katana blade tip is a
# metre out, so the blade would be handed to whichever spine or thigh bone came
# nearest. phase1b moves each prop by ITS OWN bone's pose delta; phase4a binds
# it to the Mixamo counterpart at full weight.
PROPS = {
    SWORD: ("Spine2", "mixamorig:Spine2"),        # authored sword, rides the back
    "Katana": ("hand.R", "mixamorig:RightHand"),  # low-poly-katana, right fist
}

# raylib's skinning shader reads exactly 4 bone indices/weights per vertex
# (rmodels.c). Blender happily exports more; the surplus is dropped on load and
# the under-weighted vertex collapses toward the origin.
MAX_INFLUENCES = 4

# Millimetres to push apart surfaces that are coincident enough to z-fight.
ZFIGHT_EPS = 0.001
ZFIGHT_PUSH = 0.0025
ZFIGHT_PASSES = 6
ZFIGHT_SETTLE = 40
ZFIGHT_SETTLE_EPS = 0.0005

# The coat carries arm weight on vertices up to 39 cm from any arm bone -- one
# skirt vertex at hip height is 35% driven by an upper arm. Harmless in the
# authored A-pose, but swinging the arms 60 deg out to the Mixamo T-pose drags
# the skirt up into wings, and in game every arm raise would do the same. Arm
# influence is kept in full within ARM_KEEP of an arm bone (sleeves, shoulder
# cape), tapered to nothing by ARM_CUT.
ARM_PREFIX = ("shoulder.", "upper_arm.", "forearm.", "hand.", "thumb.", "f_middle.")
ARM_KEEP = 0.12
ARM_CUT = 0.22


def bone_map():
    """Model bone -> Mixamo bone. Every one of the model's 40 bones maps."""
    m = {
        "Hips": "mixamorig:Hips",
        "Spine1": "mixamorig:Spine1",
        "Spine2": "mixamorig:Spine2",
        "Neck": "mixamorig:Neck",
        "Head": "mixamorig:Head",
        "Head_end": "mixamorig:HeadTop_End",
    }
    for src, dst in (("shoulder", "Shoulder"), ("upper_arm", "Arm"),
                     ("forearm", "ForeArm"), ("hand", "Hand")):
        for suffix, side in (("L", "Left"), ("R", "Right")):
            m["%s.%s" % (src, suffix)] = "mixamorig:%s%s" % (side, dst)
    for num, idx in (("01", "1"), ("02", "2"), ("03", "3")):
        for suffix, side in (("L", "Left"), ("R", "Right")):
            m["thumb.%s.%s" % (num, suffix)] = "mixamorig:%sHandThumb%s" % (side, idx)
            m["f_middle.%s.%s" % (num, suffix)] = "mixamorig:%sHandMiddle%s" % (side, idx)
    for suffix, side in (("L", "Left"), ("R", "Right")):
        m["thumb.03.%s_end" % suffix] = "mixamorig:%sHandThumb4" % side
        m["f_middle.03.%s_end" % suffix] = "mixamorig:%sHandMiddle4" % side
        m["thigh.%s" % suffix] = "mixamorig:%sUpLeg" % side
        m["shin.%s" % suffix] = "mixamorig:%sLeg" % side
        m["foot.%s" % suffix] = "mixamorig:%sFoot" % side
        m["toe.%s" % suffix] = "mixamorig:%sToeBase" % side
        m["toe.%s_end" % suffix] = "mixamorig:%sToe_End" % side
    return m


def _activate(obj, mode="OBJECT"):
    """Make `obj` the active object in OBJECT mode, then optionally switch mode.

    Freshly opening a .blend leaves no active object at all, and most of the rig
    in this file is hidden, so both have to be handled before any operator runs.
    """
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
    """Bones parent-first, so a pose applied in order propagates down."""
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
    return [o for o in bpy.data.objects if o.type == "MESH" and o.parent is armature
            and o.name not in PROPS]


def _segment_distance(p, a, b):
    ab = b - a
    t = max(0.0, min(1.0, (p - a).dot(ab) / max(ab.length_squared, 1e-12)))
    return (p - (a + ab * t)).length


# ---------------------------------------------------------------------------
# Phase 0 -- stop garments from following the arms
# ---------------------------------------------------------------------------
def phase0_taper_arm_weights():
    """Strip arm influence from vertices nowhere near an arm bone.

    Runs BEFORE the T-pose swing, because the swing bakes these weights into the
    mesh: leaving them in place flares the coat skirt into triangular wings.
    A vertex that loses all its weight is reassigned to whichever non-arm bone it
    is closest to, which for the skirt is the spine, hips or thighs.
    """
    sek = bpy.data.objects[SEKIRO_ARM]
    bones = {b.name: (b.head_local.copy(), b.tail_local.copy()) for b in sek.data.bones}
    arm_bones = [n for n in bones if n.startswith(ARM_PREFIX)]
    other_bones = [n for n in bones if not n.startswith(ARM_PREFIX)]

    report = []
    for obj in _skinned_meshes(sek):
        names = {i: g.name for i, g in enumerate(obj.vertex_groups)}
        groups = {g.name: g for g in obj.vertex_groups}
        tapered = 0
        rescued = 0
        for v in obj.data.vertices:
            arms = [(names[g.group], g.weight) for g in v.groups
                    if names[g.group].startswith(ARM_PREFIX) and g.weight > 1e-6]
            if not arms:
                continue
            d = min(_segment_distance(v.co, *bones[n]) for n in arm_bones)
            if d <= ARM_KEEP:
                continue
            factor = 0.0 if d >= ARM_CUT else (ARM_CUT - d) / (ARM_CUT - ARM_KEEP)
            keep = [(names[g.group], g.weight) for g in v.groups
                    if not names[g.group].startswith(ARM_PREFIX) and g.weight > 1e-6]
            scaled = [(n, w * factor) for n, w in arms if w * factor > 1e-6]
            for name, _ in arms:
                groups[name].remove([v.index])
            for name, w in scaled:
                groups[name].add([v.index], w, "REPLACE")
            total = sum(w for _, w in keep) + sum(w for _, w in scaled)
            if total <= 1e-6:
                nearest = min(other_bones,
                              key=lambda n: _segment_distance(v.co, *bones[n]))
                if nearest not in groups:
                    groups[nearest] = obj.vertex_groups.new(name=nearest)
                groups[nearest].add([v.index], 1.0, "REPLACE")
                rescued += 1
            tapered += 1
        if tapered:
            report.append({"mesh": obj.name, "verts_tapered": tapered,
                           "verts_reassigned": rescued})
    return {"tapered": report, "keep_m": ARM_KEEP, "cut_m": ARM_CUT}


# ---------------------------------------------------------------------------
# Phase 1 -- swing the model into the Mixamo rest directions
# ---------------------------------------------------------------------------
def phase1_align_to_mixamo_rest():
    """Rotate each model bone onto its Mixamo counterpart's rest DIRECTION.

    Directions only: bone lengths are left alone, so the character keeps the
    artist's proportions (long forearms, low ankles) and only its pose changes.
    Each bone gets the minimal-arc rotation, which introduces no twist of its
    own -- roll is inherited from the parent chain.
    """
    sek = bpy.data.objects[SEKIRO_ARM]
    main = bpy.data.objects[MAIN_ARM]
    mapping = bone_map()
    mw = main.matrix_world

    targets = {}
    for bone in main.data.bones:
        head, tail = mw @ bone.head_local, mw @ bone.tail_local
        targets[bone.name] = (tail - head).normalized()

    _activate(sek, "POSE")
    for pb in sek.pose.bones:
        pb.matrix_basis = Matrix.Identity(4)
    bpy.context.view_layer.update()

    applied = []
    for bone in _hierarchy(sek):
        target = mapping.get(bone.name)
        if target is None:
            continue
        pb = sek.pose.bones[bone.name]
        cur = (pb.tail - pb.head).normalized()
        tgt = targets[target]
        rot = cur.rotation_difference(tgt).to_matrix().to_4x4()
        head = pb.head.copy()
        pb.matrix = (Matrix.Translation(head) @ rot @ Matrix.Translation(-head)
                     @ pb.matrix)
        bpy.context.view_layer.update()
        applied.append({"bone": bone.name,
                        "deg": round(cur.angle(tgt) * 57.29577951, 2)})

    # Residual check: every mapped bone must now point along its target.
    worst = 0.0
    for bone in _hierarchy(sek):
        target = mapping.get(bone.name)
        if target is None:
            continue
        pb = sek.pose.bones[bone.name]
        cur = (pb.tail - pb.head).normalized()
        worst = max(worst, cur.angle(targets[target]) * 57.29577951)

    bpy.ops.object.mode_set(mode="OBJECT")
    return {"rotated": len(applied), "max_residual_deg": round(worst, 4),
            "largest_swings": sorted(applied, key=lambda r: -r["deg"])[:6]}


def phase1b_bake_pose_into_meshes():
    """Freeze the aligned pose into the mesh data, then make it the rest pose.

    A prop carries no armature modifier, so it is moved by hand with the delta
    of the bone it is about to be bound to. The move is applied to matrix_world
    rather than the mesh data because a prop may sit on its own transform -- the
    katana does. (For a prop whose matrix_world is identity the two are the same
    thing, which is what the back-sword relied on.)
    """
    sek = bpy.data.objects[SEKIRO_ARM]
    meshes = _skinned_meshes(sek)

    moved = []
    for name, (model_bone, _) in PROPS.items():
        prop = bpy.data.objects.get(name)
        if prop is None:
            continue
        pb = sek.pose.bones[model_bone]
        rest = sek.data.bones[model_bone].matrix_local
        delta = pb.matrix @ rest.inverted()
        prop.matrix_world = delta @ prop.matrix_world
        moved.append(name)
    bpy.context.view_layer.update()

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

    _activate(sek, "POSE")
    bpy.ops.pose.armature_apply()
    bpy.ops.object.mode_set(mode="OBJECT")

    # Rebind: applying the modifier removed it, and the meshes still need to
    # follow the model rig until phase 3 moves them to the Mixamo rig.
    for obj in meshes:
        mod = obj.modifiers.new(name="Armature", type="ARMATURE")
        mod.object = sek
    return {"baked": baked, "n": len(baked)}


# ---------------------------------------------------------------------------
# Phase 2 -- rebuild the Mixamo rest skeleton onto the model's joints
# ---------------------------------------------------------------------------
def phase2_rebuild_rest():
    """Move every Mixamo rest bone onto the model's joint, orientation intact.

    Each bone keeps its direction vector and its roll, so its rest ORIENTATION is
    bit-identical and the clips -- which are rotations relative to that
    orientation -- keep their meaning. Only head position and length change.

    Bones the model has no counterpart for (Spine, eyes, index/ring/pinky,
    Sword_joint, Shield_joint) carry no weight. `Spine` sits BETWEEN two mapped
    bones, so it is re-interpolated along the new Hips->Spine1 segment to stay a
    sane pivot; the rest simply ride their nearest mapped ancestor.
    """
    sek = bpy.data.objects[SEKIRO_ARM]
    main = bpy.data.objects[MAIN_ARM]
    mapping = bone_map()
    inverse = {v: k for k, v in mapping.items()}
    mw_inv = main.matrix_world.inverted()
    scale = main.matrix_world.to_scale().x

    src = {b.name: (b.head_local.copy(), b.tail_local.copy(), b.length)
           for b in sek.data.bones}
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

    # `Spine` is the one unmapped bone with a mapped descendant: keep it at the
    # same fractional height along the (now rebuilt) Hips -> Spine1 segment.
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

    # Everything else rides the nearest ancestor that did move.
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

    # Orientation must be untouched, or the clips change meaning.
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
def phase3_rebind():
    sek = bpy.data.objects[SEKIRO_ARM]
    main = bpy.data.objects[MAIN_ARM]
    mapping = bone_map()
    meshes = _skinned_meshes(sek)

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
def phase4a_bind_props():
    """Rigid-bind each prop to one bone so it rides that joint through every clip.

    Every vertex gets full weight on a single bone, which is what makes the prop
    STATIC in that joint's frame -- it cannot bend or stretch, it only follows.
    """
    main = bpy.data.objects[MAIN_ARM]
    report = {}
    for name, (_, mixamo_bone) in PROPS.items():
        prop = bpy.data.objects.get(name)
        if prop is None:
            report[name] = None
            continue
        world = prop.matrix_world.copy()
        prop.parent = None
        prop.matrix_world = world

        for vg in list(prop.vertex_groups):
            prop.vertex_groups.remove(vg)
        vg = prop.vertex_groups.new(name=mixamo_bone)
        vg.add(range(len(prop.data.vertices)), 1.0, "REPLACE")

        if not any(m.type == "ARMATURE" for m in prop.modifiers):
            mod = prop.modifiers.new(name="Armature", type="ARMATURE")
            mod.object = main
        else:
            for m in prop.modifiers:
                if m.type == "ARMATURE":
                    m.object = main

        prop.parent = main
        prop.matrix_parent_inverse = main.matrix_world.inverted()
        prop.matrix_world = world
        report[name] = {"bound_to": mixamo_bone, "verts": len(prop.data.vertices)}
    return report


def phase4b_break_coincident_surfaces():
    """Nudge apart surfaces close enough to z-fight.

    Every vertex sitting within ZFIGHT_EPS of another shell is pushed along its
    own normal, in whichever direction increases the gap. Outer garment shells
    move outward, inner ones sink inward; either way the surfaces stop being
    coplanar. 2.5 mm on a 1.85 m character is 0.13% of height -- invisible.

    Iterated, because the trees are a snapshot: moving one shell invalidates the
    surface its neighbour was measured against, and two shells can be nudged
    toward each other in the same pass. One pass left 24 coincident vertices.
    """
    main = bpy.data.objects[MAIN_ARM]
    meshes = [o for o in bpy.data.objects if o.type == "MESH" and o.parent is main]

    passes = []
    for _ in range(ZFIGHT_PASSES):
        trees = {}
        for obj in meshes:
            verts = [obj.matrix_world @ v.co for v in obj.data.vertices]
            polys = [list(p.vertices) for p in obj.data.polygons]
            trees[obj.name] = BVHTree.FromPolygons(verts, polys, all_triangles=False)

        moved = []
        for obj in meshes:
            mw = obj.matrix_world
            inv = mw.inverted()
            nudged = 0
            for v in obj.data.vertices:
                world_co = mw @ v.co
                normal = (mw.to_3x3() @ v.normal).normalized()
                if normal.length < 0.5:
                    continue
                closest = None
                for other in meshes:
                    if other.name == obj.name:
                        continue
                    hit = trees[other.name].find_nearest(world_co, ZFIGHT_EPS)
                    if hit[0] is not None and (closest is None or hit[3] < closest[3]):
                        closest = (other.name, hit[0], hit[2], hit[3])
                if closest is None:
                    continue
                tree = trees[closest[0]]
                out = tree.find_nearest(world_co + normal * ZFIGHT_PUSH, 1.0)
                back = tree.find_nearest(world_co - normal * ZFIGHT_PUSH, 1.0)
                d_out = out[3] if out[0] is not None else 1.0
                d_back = back[3] if back[0] is not None else 1.0
                sign = 1.0 if d_out >= d_back else -1.0
                v.co = inv @ (world_co + normal * ZFIGHT_PUSH * sign)
                nudged += 1
            if nudged:
                obj.data.update()
                moved.append({"mesh": obj.name, "verts_nudged": nudged})
        passes.append(sum(m["verts_nudged"] for m in moved))
        if not moved:
            break

    # A handful of vertices survive the sweeps by oscillating: two shells push
    # each other apart in alternate passes and neither settles. Finish them one
    # at a time, worst first, rebuilding the trees after every single move so no
    # decision is ever made against a stale surface.
    settled = []
    for _ in range(ZFIGHT_SETTLE):
        trees = {}
        for obj in meshes:
            verts = [obj.matrix_world @ v.co for v in obj.data.vertices]
            polys = [list(p.vertices) for p in obj.data.polygons]
            trees[obj.name] = BVHTree.FromPolygons(verts, polys, all_triangles=False)
        worst = None
        for obj in meshes:
            mw = obj.matrix_world
            for v in obj.data.vertices:
                world_co = mw @ v.co
                for other in meshes:
                    if other.name == obj.name:
                        continue
                    # Tighter than ZFIGHT_EPS on purpose: the sweeps aim for
                    # clearance, the settle only has to clear the depth-buffer
                    # resolution. Triggering at the full epsilon just ping-pongs
                    # a vertex that is already far enough apart.
                    hit = trees[other.name].find_nearest(world_co, ZFIGHT_SETTLE_EPS)
                    if hit[0] is None:
                        continue
                    if worst is None or hit[3] < worst[3]:
                        worst = (obj, v.index, other.name, hit[3])
        if worst is None:
            break
        obj, vi, other_name, dist = worst
        mw = obj.matrix_world
        v = obj.data.vertices[vi]
        world_co = mw @ v.co
        normal = (mw.to_3x3() @ v.normal).normalized()
        tree = trees[other_name]
        push = ZFIGHT_PUSH * 1.5
        out = tree.find_nearest(world_co + normal * push, 1.0)
        back = tree.find_nearest(world_co - normal * push, 1.0)
        d_out = out[3] if out[0] is not None else 1.0
        d_back = back[3] if back[0] is not None else 1.0
        sign = 1.0 if d_out >= d_back else -1.0
        v.co = mw.inverted() @ (world_co + normal * push * sign)
        obj.data.update()
        settled.append({"mesh": obj.name, "v": vi, "vs": other_name,
                        "was_mm": round(dist * 1000, 3)})
    return {"push_mm": ZFIGHT_PUSH * 1000, "nudges_per_pass": passes,
            "settled": settled}


def phase4c_strip_and_prune():
    """Drop colour attributes and the Paladin.

    Colour attributes no material reads still export as COLOR_0, and this game's
    skinning shader multiplies it into finalColor -- a black or zero-alpha
    attribute renders the model black. See assets/shaders/glsl330/skinning.fs.
    """
    main = bpy.data.objects[MAIN_ARM]
    sek = bpy.data.objects.get(SEKIRO_ARM)

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
    if sek is not None:
        removed.append(sek.name)
        bpy.data.objects.remove(sek, do_unlink=True)

    kept = [o.name for o in bpy.data.objects if o.type == "MESH" and o.parent is main]
    return {"color_attrs_stripped": stripped, "removed": removed, "skin": kept}


# ---------------------------------------------------------------------------
# Phase 5 -- couple garments to the body they cover
# ---------------------------------------------------------------------------
GARMENTS = ("Plane", "Jacket", "Scarf", "Cube.006")
COUPLE_FULL = 0.03
COUPLE_MAX = 0.08

# Cloth hanging BETWEEN two limbs must not be coupled to either one. The coat
# skirt is authored on a both-thighs blend so it stays centred; coupling its hem
# to whichever leg happened to be nearest made it track that leg and let the
# other one swing straight through -- measured 42 mm of poke-through before,
# 339 mm after. Where the body underneath is mostly leg, the authored weights
# win.
LEG_BONES = tuple("mixamorig:%s%s" % (side, part)
                  for side in ("Left", "Right")
                  for part in ("UpLeg", "Leg", "Foot", "ToeBase", "Toe_End"))
LEG_DOMINANT = 0.5


def phase5_couple_garment_weights():
    """Blend each garment vertex's weights toward the body surface beneath it.

    Measured need: the torso shell is arm-weighted while the jacket over it is
    spine/thigh-weighted, so on Turn180 and Crouching the torso drove one way,
    the jacket another, and the body punched 156 mm through the cloth. Two
    surfaces in contact can only stay in contact if they are driven by the same
    bones, so within COUPLE_FULL of the body a garment vertex simply adopts the
    body's weights, fading back to its own by COUPLE_MAX. Free-hanging cloth --
    the coat hem, the scarf tails -- is beyond that and keeps its authored
    weights, so it still swings.

    Runs AFTER the rename to mixamorig:*, and re-caps influences at 4 because
    blending two weight sets can exceed the limit.
    """
    main_arm = bpy.data.objects[MAIN_ARM]
    objs = {o.name: o for o in bpy.data.objects
            if o.type == "MESH" and o.parent is main_arm}
    garments = [objs[n] for n in GARMENTS if n in objs]
    bodies = [o for n, o in objs.items() if n not in GARMENTS and n not in PROPS]

    baked = {}
    for obj in bodies:
        mw = obj.matrix_world
        names = {i: g.name for i, g in enumerate(obj.vertex_groups)}
        weights = []
        for v in obj.data.vertices:
            weights.append({names[g.group]: g.weight for g in v.groups
                            if g.weight > 1e-6})
        baked[obj.name] = {
            "co": [mw @ v.co for v in obj.data.vertices],
            "w": weights,
            "tree": BVHTree.FromPolygons([mw @ v.co for v in obj.data.vertices],
                                         [list(p.vertices) for p in obj.data.polygons],
                                         all_triangles=False),
            "poly": [list(p.vertices) for p in obj.data.polygons],
        }

    report = []
    for obj in garments:
        mw = obj.matrix_world
        groups = {g.name: g for g in obj.vertex_groups}
        names = {i: g.name for i, g in enumerate(obj.vertex_groups)}
        coupled = 0
        for v in obj.data.vertices:
            world = mw @ v.co
            best = None
            for src in bodies:
                data = baked[src.name]
                loc, nor, idx, dist = data["tree"].find_nearest(world, COUPLE_MAX)
                if loc is not None and (best is None or dist < best[3]):
                    best = (src.name, idx, loc, dist)
            if best is None:
                continue
            src_name, idx, loc, dist = best
            data = baked[src_name]
            alpha = 1.0 if dist <= COUPLE_FULL else \
                (COUPLE_MAX - dist) / (COUPLE_MAX - COUPLE_FULL)

            # inverse-distance blend of the hit face's corner weights
            corners = data["poly"][idx]
            total = 0.0
            body_w = {}
            for ci in corners:
                d = max((loc - data["co"][ci]).length, 1e-5)
                k = 1.0 / (d * d)
                total += k
                for bone, w in data["w"][ci].items():
                    body_w[bone] = body_w.get(bone, 0.0) + k * w
            if total <= 0:
                continue
            body_w = {b: w / total for b, w in body_w.items()}
            if sum(w for b, w in body_w.items() if b in LEG_BONES) > LEG_DOMINANT:
                continue

            own = {names[g.group]: g.weight for g in v.groups if g.weight > 1e-6}
            merged = {}
            for bone in set(own) | set(body_w):
                merged[bone] = (1.0 - alpha) * own.get(bone, 0.0) \
                    + alpha * body_w.get(bone, 0.0)
            merged = {b: w for b, w in merged.items() if w > 1e-6}
            if not merged:
                continue
            norm = sum(merged.values())

            for bone in own:
                groups[bone].remove([v.index])
            for bone, w in merged.items():
                if bone not in groups:
                    groups[bone] = obj.vertex_groups.new(name=bone)
                    names = {i: g.name for i, g in enumerate(obj.vertex_groups)}
                groups[bone].add([v.index], w / norm, "REPLACE")
            coupled += 1

        _activate(obj)
        bpy.ops.object.vertex_group_limit_total(group_select_mode="ALL",
                                                limit=MAX_INFLUENCES)
        bpy.ops.object.vertex_group_normalize_all(group_select_mode="ALL",
                                                  lock_active=False)
        report.append({"garment": obj.name, "verts_coupled": coupled,
                       "of": len(obj.data.vertices)})
    return {"coupled": report, "full_m": COUPLE_FULL, "fade_m": COUPLE_MAX}


def main():
    report = {}
    report["orientations"] = snapshot_orientations()
    report["phase0"] = phase0_taper_arm_weights()
    report["phase1"] = phase1_align_to_mixamo_rest()
    report["phase1b"] = phase1b_bake_pose_into_meshes()
    report["phase2"] = phase2_rebuild_rest()
    report["phase3"] = phase3_rebind()
    report["phase4a"] = phase4a_bind_props()
    # Prune BEFORE nudging: the Paladin occupies the same space as the new
    # model, so leaving it in makes phase4b push Sekiro vertices away from
    # Paladin surfaces that are about to be deleted.
    report["phase4c"] = phase4c_strip_and_prune()
    report["phase4b"] = phase4b_break_coincident_surfaces()
    report["phase5"] = phase5_couple_garment_weights()
    print("RETARGET_RESULT " + json.dumps(report))
    return report
