"""Author the katana guard and rebake the Block, BlockWalk and Impact clips.

The stock Block, BlockWalk and Impact come from Mixamo's "Pro Sword and Shield
Pack": the left arm raises a shield across the body while the sword arm drops
away. With a katana in the right fist that reads as nothing at all, so all three
are replaced with a two-handed horizontal guard -- blade level in front of the
chest, edge up, flat presented forward.

Impact is the guard-impact flinch (PlayerAnimState::GuardImpact, and the
Swordman's too), so it also carries a recoil: the guard is driven down and back
on the blow, then recovers. Its length is left at the stock 22 frames because
the runtime takes the flinch's duration from the clip itself.

Run against the LIVE session of pack_sekiro.blend, AFTER tools/add_katana.py
and BEFORE tools/merge_animations.py:

    exec(open("tools/make_katana_block.py").read())
    report = main()

Nothing here is keyed by hand. The katana is rigid-bound to `mixamorig:RightHand`
(see add_katana.py), so the pose is solved BACKWARDS: pick the world transform
the katana should have, invert the bind offset to get the wrist transform that
produces it, then run an analytic two-bone IK up the arm to reach it. That is
what guarantees the blade is level rather than approximately level.

The left fist lands further up the same grip. Both hands on a katana point the
same way, so the left wrist reuses the right wrist's rotation and the same
wrist->palm offset carries over unchanged.

The guard is stored RELATIVE TO THE CHEST (`mixamorig:Spine2`), not in absolute
armature space. Storing it absolutely would pin the arms in space while the legs
walked underneath; chest-relative, the guard rides the torso the way held hands
actually do. The cost is that the guard yaws with the chest -- the Idle chest
sits 26 deg off rest -- so the blade's heading swings a little through a clip
while its TILT stays within about 2 deg of level. Level is what reads; heading
is not.

Lower bodies are untouched. Only the eight arm bones are overwritten, so Block
keeps Idle's stance and BlockWalk keeps Walk's stride, hips and net travel --
this script asserts that travel is bit-identical to Walk's, because the runtime
takes BlockWalk's displacement from the clip's root motion.
"""

import math

import bpy
from mathutils import Matrix, Vector

MAIN_ARM = "Armature"
CHEST = "mixamorig:Spine2"
HAND_MESH = "Hand"
KATANA = "Katana"

# Parent-first: setting a parent's matrix moves its children, so the chain has
# to be written shoulder -> hand.
GUARD_BONES = [
    "mixamorig:RightShoulder", "mixamorig:RightArm",
    "mixamorig:RightForeArm", "mixamorig:RightHand",
    "mixamorig:LeftShoulder", "mixamorig:LeftArm",
    "mixamorig:LeftForeArm", "mixamorig:LeftHand",
]

# Where the right fist holds the katana, in world metres. Chest height, a third
# of a metre clear of the sternum, just right of centre so the blade crosses the
# body and both hands stay inside the arms' 0.58 m reach.
HOLD_WORLD = Vector((-0.05, -0.28, 1.52))

# How far up the grip the left fist sits from the right.
LEFT_BACK = 0.135

# Frames over which the guard comes up in Block. Idle underneath it throughout.
BLOCK_FRAMES = 15
BLOCK_RAISE = 8

# GuardImpact: the guard is driven DOWN and back into the body by the blow, then
# springs out. A level blade catching a descending strike gets pushed toward the
# holder, so the recoil is mostly -Z with a little +Y (backwards; the model faces
# -Y) and a tip dip.
RECOIL_OFFSET = Vector((0.0, 0.05, -0.09))
RECOIL_TILT_DEG = 10.0
RECOIL_PEAK = 2       # frame index the blow lands on
RECOIL_SETTLE = 13    # frame index the guard is steady again

# Base actions are named, not read off the target object: Impact overwrites the
# very armature it samples, so a second run would otherwise use its own output as
# the base and compound the recoil.
SOURCES = {
    "Block": ("mixamo.com.005", "Armature.050", "BlockKatana"),        # base Idle
    "BlockWalk": ("mixamo.com.025", "Armature.058", "BlockWalkKatana"),  # base Walk
    "Impact": ("mixamo.com.033", "Armature.033", "ImpactKatana"),      # base Impact
}


def _arm():
    return bpy.data.objects[MAIN_ARM]


def reset_pose():
    for pb in _arm().pose.bones:
        pb.matrix_basis = Matrix.Identity(4)
    bpy.context.view_layer.update()


def aim_matrix(head, direction, ref_x):
    """Armature-space bone matrix with +Y along `direction`, X kept near `ref_x`."""
    y = Vector(direction).normalized()
    x = Vector(ref_x) - y * Vector(ref_x).dot(y)
    if x.length < 1e-6:
        x = Vector((1, 0, 0)) - y * y.x
    x.normalize()
    z = x.cross(y).normalized()
    x = y.cross(z).normalized()
    return Matrix(((x.x, y.x, z.x, head.x),
                   (x.y, y.y, z.y, head.y),
                   (x.z, y.z, z.z, head.z),
                   (0, 0, 0, 1)))


def set_bone_world(name, world_mat):
    """Set a bone from a WORLD matrix, dropping the armature's 0.01 object scale.

    The rig sits at scale 0.01, so armature space is centimetres. Converting a
    world matrix through matrix_world.inverted() drags a 100x into the rotation
    block, which has to be renormalised away or the bone inherits the scale.
    """
    arm = _arm()
    am = arm.matrix_world.inverted() @ world_mat
    loc = am.translation.copy()
    r = am.to_3x3()
    for i in range(3):
        c = Vector((r[0][i], r[1][i], r[2][i])).normalized()
        r[0][i], r[1][i], r[2][i] = c.x, c.y, c.z
    m = r.to_4x4()
    m.translation = loc
    arm.pose.bones[name].matrix = m
    bpy.context.view_layer.update()


def two_bone_ik(upper, fore, target_a, pole_a):
    """Elbow position for a chain reaching `target_a`, read off the CURRENT head."""
    arm = _arm()
    S = arm.pose.bones[upper].matrix.translation.copy()
    l1 = arm.data.bones[upper].length
    l2 = arm.data.bones[fore].length
    v = Vector(target_a) - S
    d = max(abs(l1 - l2) + 1e-4, min(l1 + l2 - 1e-4, v.length))
    axis = v.normalized()
    a = (l1 * l1 - l2 * l2 + d * d) / (2 * d)
    h = math.sqrt(max(l1 * l1 - a * a, 0.0))
    p = Vector(pole_a)
    p = p - axis * p.dot(axis)
    if p.length < 1e-6:
        p = Vector((0, 0, 1)) - axis * axis.z
    p.normalize()
    return S, S + axis * a + p * h, d, l1 + l2


def ik_arm(side, wrist_world, pole_world, hand_world):
    arm = _arm()
    inv = arm.matrix_world.inverted()
    U, F, H = ("mixamorig:%s%s" % (side, s) for s in ("Arm", "ForeArm", "Hand"))
    wrist_a = inv @ Vector(wrist_world)
    pole_a = (inv.to_3x3() @ Vector(pole_world)).normalized()
    S, elbow, d, reach = two_bone_ik(U, F, wrist_a, pole_a)
    if d > reach - 0.5:
        raise RuntimeError("%s arm cannot reach: %.2f of %.2f" % (side, d, reach))
    ub, fb = arm.data.bones[U], arm.data.bones[F]
    arm.pose.bones[U].matrix = aim_matrix(S, elbow - S, Vector(ub.matrix_local.col[0][:3]))
    bpy.context.view_layer.update()
    e = arm.pose.bones[F].matrix.translation.copy()
    arm.pose.bones[F].matrix = aim_matrix(e, wrist_a - e, Vector(fb.matrix_local.col[0][:3]))
    bpy.context.view_layer.update()
    set_bone_world(H, hand_world)
    return {"reach": round(d, 2), "limit": round(reach, 2)}


def aim_shoulder(side, toward_world):
    arm = _arm()
    B = "mixamorig:%sShoulder" % side
    head = arm.pose.bones[B].matrix.translation.copy()
    tgt = arm.matrix_world.inverted() @ Vector(toward_world)
    arm.pose.bones[B].matrix = aim_matrix(head, tgt - head,
                                          Vector(arm.data.bones[B].matrix_local.col[0][:3]))
    bpy.context.view_layer.update()


def katana_grip_axis():
    """(hold_y, grip sign, convex side) measured off the katana mesh, as add_katana does."""
    k = bpy.data.objects[KATANA]
    co = [v.co.copy() for v in k.data.vertices]
    lo, hi = min(p.y for p in co), max(p.y for p in co)
    best = None
    for i in range(40):
        a = lo + (hi - lo) * i / 40
        b = lo + (hi - lo) * (i + 1) / 40
        sl = [p for p in co if a <= p.y <= b]
        if not sl:
            continue
        cx = sum(p.x for p in sl) / len(sl)
        cz = sum(p.z for p in sl) / len(sl)
        r = max(((p.x - cx) ** 2 + (p.z - cz) ** 2) ** 0.5 for p in sl)
        if best is None or r > best[1]:
            best = ((a + b) / 2, r)
    tsuba_y = best[0]
    sign = 1.0 if (hi - tsuba_y) < (tsuba_y - lo) else -1.0
    bins = {}
    for p in co:
        if (p.y - tsuba_y) * sign < 0:
            bins.setdefault(round(p.y, 2), []).append(p.z)
    line = sorted((y, sum(z) / len(z)) for y, z in bins.items() if len(z) >= 4)
    (y0, z0), (y1, z1) = line[0], line[-1]
    bulge = sum(z - (z0 + (z1 - z0) * (y - y0) / (y1 - y0)) for y, z in line)
    return tsuba_y + sign * 0.085, sign, (1.0 if bulge > 0 else -1.0)


def guard_katana_matrix(hold_world, hold_y, sign, convex_z, tilt_deg=0.0):
    """Blade level, tip to the character's left, edge up, flat facing forward.

    `tilt_deg` rotates the whole katana about the world forward axis, around the
    hold point, which dips the tip without moving the fists -- the recoil read.
    """
    # tip is local -Y*sign and must point to the character's left, so +Y maps to -X.
    y_img = Vector((-1, 0, 0)) * sign
    # the edge is convex_z * local Z, and the edge must point up, so local Z maps
    # to +Z scaled by convex_z -- NOT to -Z, which lays the blade edge-down and
    # presents its narrow side to the camera instead of its flat.
    z_img = Vector((0, 0, 1)) * convex_z
    x_img = y_img.cross(z_img)
    R = Matrix(((x_img.x, y_img.x, z_img.x),
                (x_img.y, y_img.y, z_img.y),
                (x_img.z, y_img.z, z_img.z))).to_4x4()
    K = (Matrix.Translation(Vector(hold_world)) @ R
         @ Matrix.Translation(Vector((0, hold_y, 0))).inverted())
    if tilt_deg:
        piv = Matrix.Translation(Vector(hold_world))
        K = piv @ Matrix.Rotation(math.radians(tilt_deg), 4, "Y") @ piv.inverted() @ K
    return K


def build_guard(hold_world=HOLD_WORLD, tilt_deg=0.0):
    """Pose both arms onto the katana."""
    arm = _arm()
    k = bpy.data.objects[KATANA]
    reset_pose()
    hold_y, sign, convex_z = katana_grip_axis()

    # the katana's offset inside the hand bone, fixed by the rigid bind
    offset = (arm.matrix_world
              @ arm.data.bones["mixamorig:RightHand"].matrix_local).inverted() @ k.matrix_world

    K = guard_katana_matrix(hold_world, hold_y, sign, convex_z, tilt_deg)
    Hr = K @ offset.inverted()

    aim_shoulder("Right", Vector((-0.20, -0.02, 1.50)))
    aim_shoulder("Left", Vector((0.20, -0.02, 1.50)))
    r = ik_arm("Right", Hr.translation, Vector((-0.55, 0.55, -0.62)), Hr)

    d = Vector(hold_world) - Hr.translation          # wrist -> palm, same for both fists
    left_grip = K @ Vector((0, hold_y + LEFT_BACK * sign, 0))
    Hl = Matrix.Translation(left_grip - d) @ Hr.to_3x3().to_4x4()
    l = ik_arm("Left", Hl.translation, Vector((0.55, 0.55, -0.62)), Hl)
    return {"right": r, "left": l}


def capture_relative():
    """Guard arm matrices expressed against the chest."""
    arm = _arm()
    C = arm.pose.bones[CHEST].matrix.copy()
    return {b: C.inverted() @ arm.pose.bones[b].matrix.copy() for b in GUARD_BONES}


def blend(a, b, t):
    if t <= 0:
        return a.copy()
    if t >= 1:
        return b.copy()
    la, qa, sa = a.decompose()
    lb, qb, sb = b.decompose()
    return Matrix.LocRotScale(la.lerp(lb, t), qa.slerp(qb, t), sa.lerp(sb, t))


def blend_rel(a, b, t):
    return {k: blend(a[k], b[k], t) for k in a}


def sample(base_action, base_slot, frames, pose_fn):
    """Play the base clip and lay the guard over the arms, frame by frame.

    `pose_fn(i)` returns the chest-relative guard for that frame and how much of
    it to mix in, so a clip can both fade the guard up (Block) and move it around
    once it is up (the Impact recoil).
    """
    arm = _arm()
    ad = arm.animation_data or arm.animation_data_create()
    ad.action = base_action
    ad.action_slot = base_slot
    scn = bpy.context.scene
    out = []
    for i, f in enumerate(frames):
        scn.frame_set(f)
        bpy.context.view_layer.update()
        was = {b: arm.pose.bones[b].matrix.copy() for b in GUARD_BONES}
        C = arm.pose.bones[CHEST].matrix.copy()
        rel, w = pose_fn(i)
        for b in GUARD_BONES:
            arm.pose.bones[b].matrix = blend(was[b], C @ rel[b], w)
            bpy.context.view_layer.update()
        out.append({pb.name: pb.matrix_basis.decompose() for pb in arm.pose.bones})
    return out


def write_action(name, snaps, frames):
    """Key every bone on every frame, so the clip stands alone."""
    arm = _arm()
    old = bpy.data.actions.get(name)
    if old:
        bpy.data.actions.remove(old)
    act = bpy.data.actions.new(name)
    slot = act.slots.new(id_type="OBJECT", name="Armature")
    bag = act.layers.new("Layer").strips.new(type="KEYFRAME").channelbags.new(slot)
    for pb in arm.pose.bones:
        stem = 'pose.bones["%s"].' % pb.name
        for path, size, part in (("location", 3, 0), ("rotation_quaternion", 4, 1),
                                 ("scale", 3, 2)):
            for c in range(size):
                fc = bag.fcurves.new(stem + path, index=c)
                fc.keyframe_points.add(len(frames))
                flat = []
                for i, f in enumerate(frames):
                    flat += [float(f), float(snaps[i][pb.name][part][c])]
                fc.keyframe_points.foreach_set("co", flat)
                for kp in fc.keyframe_points:
                    kp.interpolation = "LINEAR"
                fc.update()
    act.use_fake_user = True
    return act, slot


def _hips_travel(action, slot, first, last):
    arm = _arm()
    ad = arm.animation_data
    ad.action = action
    ad.action_slot = slot
    scn = bpy.context.scene
    pts = []
    for f in (first, last):
        scn.frame_set(f)
        bpy.context.view_layer.update()
        pts.append(arm.pose.bones["mixamorig:Hips"].matrix.translation.copy())
    return pts[1] - pts[0]


def main():
    arm = _arm()
    if bpy.data.objects.get(KATANA) is None:
        raise RuntimeError("no Katana in the file -- run tools/add_katana.py first")

    report = {"guard": build_guard()}
    rel = capture_relative()
    build_guard(HOLD_WORLD + RECOIL_OFFSET, RECOIL_TILT_DEG)
    rel_hit = capture_relative()

    def smooth(t):
        return t * t * (3 - 2 * t)

    def recoil(i):
        """0 -> 1 as the blow lands, back to 0 as the guard recovers."""
        if i <= RECOIL_PEAK:
            return smooth(i / float(RECOIL_PEAK)) if RECOIL_PEAK else 1.0
        t = (i - RECOIL_PEAK) / float(RECOIL_SETTLE - RECOIL_PEAK)
        return 0.0 if t >= 1.0 else (1.0 - smooth(t))

    for clip, (base_action_name, target_name, action_name) in SOURCES.items():
        b_act = bpy.data.actions[base_action_name]
        # Keep the stock clip alive: Impact replaces the action on the very
        # object it samples, so without a fake user the base would be purged and
        # the clip could never be re-derived.
        b_act.use_fake_user = True
        b_slot = b_act.slots[0]
        if clip == "Block":
            frames = list(range(1, BLOCK_FRAMES + 1))
            pose_fn = lambda i: (rel, smooth(min(1.0, i / float(BLOCK_RAISE - 1))))
        elif clip == "Impact":
            frames = list(range(int(b_act.frame_range[0]), int(b_act.frame_range[1]) + 1))
            pose_fn = lambda i: (blend_rel(rel, rel_hit, recoil(i)), 1.0)
        else:
            frames = list(range(int(b_act.frame_range[0]), int(b_act.frame_range[1]) + 1))
            pose_fn = lambda i: (rel, 1.0)

        snaps = sample(b_act, b_slot, frames, pose_fn)
        act, slot = write_action(action_name, snaps, frames)

        # The runtime takes BlockWalk's displacement from the clip's root motion,
        # so its travel has to survive the overlay untouched.
        if clip == "BlockWalk":
            mine = _hips_travel(act, slot, frames[0], frames[-1])
            theirs = _hips_travel(b_act, b_slot, frames[0], frames[-1])
            if (mine - theirs).length > 1e-4:
                raise RuntimeError("BlockWalk travel drifted: %r vs %r" % (mine, theirs))
            report["travel_preserved"] = [round(v, 4) for v in mine]

        tgt = bpy.data.objects[target_name]
        if tgt.animation_data is None:
            tgt.animation_data_create()
        tgt.animation_data.action = act
        tgt.animation_data.action_slot = act.slots[0]
        report[clip] = {"action": act.name, "on": target_name, "frames": len(frames)}

    arm.animation_data.action = None
    reset_pose()
    print("KATANA_BLOCK_RESULT " + repr(report))
    return report
