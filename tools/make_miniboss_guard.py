"""Author the miniboss's greatsword guard and bake Guard, GuardWalk, GuardImpact.

    blender -b ~/Documents/3D/Model/pack_miniboss.blend \
        --python tools/make_miniboss_guard.py -- --save

Run AFTER tools/build_miniboss_pack.py, which is what puts the Idle, Walk and
HitReact these are layered over into the file.

The pose is the one in the reference: the greatsword held VERTICAL, point up,
both fists on the hilt at chest height, the blade's flat presented forward at
whatever is about to hit it. Mixamo's own "great sword blocking" clips rest the
blade across the shoulder instead, which is a different silhouette.

Solved, not keyed
-----------------
Same method as tools/make_katana_block.py, for the same reason: the sword is
rigid-bound to `mixamorig:RightHand`, so the honest way to pose it is backwards.
Choose the world transform the SWORD should have, divide out the bind offset to
get the wrist transform that produces it, then run an analytic two-bone IK up
each arm to reach that wrist. Hand-keying the shoulder and elbow until the blade
looks upright gets a blade that is upright from one camera angle.

The rotation is built as a change of basis rather than as Euler angles: the
sword's measured long axis maps to +Z (point up) and its measured flat maps to
-Y (forward, the way this rig faces), which pins all three degrees of freedom
at once and cannot silently roll the blade edge-forward.

Chest-relative, which is what makes GuardWalk work
--------------------------------------------------
The eight arm bones are captured against `mixamorig:Spine2` and re-applied
against whatever the chest is doing on each frame of the base clip. Stored in
absolute armature space instead, the arms would hang in place while the legs
walked out from under them. Riding the chest also buys the guard the base
clip's breathing for free, so the hold is not glassy.

Only the arms are overwritten. Guard keeps Idle's stance, GuardWalk keeps Walk's
stride and -- asserted below -- its exact net travel, because the runtime reads
a walk's displacement from the clip's own root motion. GuardImpact keeps the
authored body flinch of `great sword impact (3)` and adds a recoil on top: a
blow on a vertical guard drives it back into the holder and knocks the blade off
plumb, then it recovers.
"""

import math
import os
import sys

import bpy
from mathutils import Matrix, Vector

MAIN_ARM = "Armature"
CHEST = "mixamorig:Spine2"
SWORD = "MiniBoss_Sword"

# Parent-first: setting a parent's matrix moves its children, so shoulder -> hand.
GUARD_BONES = [
    "mixamorig:RightShoulder", "mixamorig:RightArm",
    "mixamorig:RightForeArm", "mixamorig:RightHand",
    "mixamorig:LeftShoulder", "mixamorig:LeftArm",
    "mixamorig:LeftForeArm", "mixamorig:LeftHand",
]

# Where the right wrist sits, in world metres. This rig faces -Y and its left is
# +X, so -X is the character's right; the fists are carried just right of centre
# and forward of the chest. Height is set against the character, not guessed:
# hips 1.27, shoulders 1.95, head 2.28.
WRIST_WORLD = Vector((-0.14, -0.44, 1.46))

# How far up the hilt the left fist sits from the right, along the blade.
LEFT_UP = 0.28

# GuardImpact. The blow lands on a blade held upright, so it drives the hold
# back into the body (+Y is backwards here) and down, and knocks the blade off
# plumb about the forward axis.
RECOIL_OFFSET = Vector((0.05, 0.13, -0.10))
RECOIL_TILT_DEG = 16.0
RECOIL_PEAK = 3
RECOIL_SETTLE = 22

# Frames over which the guard comes up in Guard, with Idle running underneath.
GUARD_RAISE = 7

# clip -> base clip it is laid over
SOURCES = {"Guard": "Idle", "GuardWalk": "Walk", "GuardImpact": "HitReact"}


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

    The rig sits at scale 0.01, so armature space is centimetres; converting a
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
    if d > reach - 0.02:
        raise RuntimeError("%s arm cannot reach the hold: %.3f of %.3f"
                           % (side, d, reach))
    ub, fb = arm.data.bones[U], arm.data.bones[F]
    arm.pose.bones[U].matrix = aim_matrix(S, elbow - S, Vector(ub.matrix_local.col[0][:3]))
    bpy.context.view_layer.update()
    e = arm.pose.bones[F].matrix.translation.copy()
    arm.pose.bones[F].matrix = aim_matrix(e, wrist_a - e, Vector(fb.matrix_local.col[0][:3]))
    bpy.context.view_layer.update()
    set_bone_world(H, hand_world)
    return {"reach": round(d, 3), "limit": round(reach, 3)}


def aim_shoulder(side, toward_world):
    arm = _arm()
    B = "mixamorig:%sShoulder" % side
    head = arm.pose.bones[B].matrix.translation.copy()
    tgt = arm.matrix_world.inverted() @ Vector(toward_world)
    arm.pose.bones[B].matrix = aim_matrix(head, tgt - head,
                                          Vector(arm.data.bones[B].matrix_local.col[0][:3]))
    bpy.context.view_layer.update()


def sword_frame():
    """The sword's measured axes, and its fixed offset inside the hand bone."""
    here = os.path.dirname(os.path.abspath(__file__))
    ns = {}
    exec(open(os.path.join(here, "add_miniboss_sword.py")).read()
         .split("if __name__")[0], ns)
    sw = bpy.data.objects[SWORD]
    axes = ns["sword_axes"](sw)
    arm = _arm()
    hand_rest = arm.matrix_world @ arm.data.bones["mixamorig:RightHand"].matrix_local
    # Sword_world = Hand_world @ B, fixed because the bind is rigid.
    B = hand_rest.inverted() @ sw.matrix_world
    up = axes["axis"] * (1.0 if axes["tip_t"] > axes["grip_end_t"] else -1.0)
    return axes, hand_rest, B, up


def guard_transform(axes, hand_rest, up, wrist_world, tilt_deg=0.0):
    """The transform that stands the sword upright with the wrist at `wrist_world`.

    Built about the WRIST rather than about a point on the blade, so the fists
    land exactly where they are asked to and the sword follows. Applied to the
    hand's rest matrix it gives the wrist pose the IK then has to reach.
    """
    # Rest basis of the sword, and where each axis has to end up.
    flat = Vector(axes["flat_axis"])
    side = Vector(up).cross(flat).normalized()
    src = Matrix(((side.x, up.x, flat.x),
                  (side.y, up.y, flat.y),
                  (side.z, up.z, flat.z)))
    # blade up, flat presented forward (-Y); the third axis follows from those.
    t_up = Vector((0.0, 0.0, 1.0))
    t_flat = Vector((0.0, -1.0, 0.0))
    t_side = t_up.cross(t_flat).normalized()
    dst = Matrix(((t_side.x, t_up.x, t_flat.x),
                  (t_side.y, t_up.y, t_flat.y),
                  (t_side.z, t_up.z, t_flat.z)))
    R = (dst @ src.inverted()).to_4x4()
    if tilt_deg:
        # About the world forward axis, through the hold: the blade leaves plumb
        # without the fists moving.
        R = Matrix.Rotation(math.radians(tilt_deg), 4, "Y") @ R
    pivot = hand_rest.translation
    return (Matrix.Translation(Vector(wrist_world)) @ R
            @ Matrix.Translation(-Vector(pivot)))


def build_guard(wrist_world=WRIST_WORLD, tilt_deg=0.0):
    """Pose both arms onto an upright greatsword."""
    reset_pose()
    axes, hand_rest, _B, up = sword_frame()
    M = guard_transform(axes, hand_rest, up, wrist_world, tilt_deg)

    Hr = M @ hand_rest
    aim_shoulder("Right", Vector((-0.22, -0.10, 1.62)))
    aim_shoulder("Left", Vector((0.22, -0.10, 1.62)))
    r = ik_arm("Right", Hr.translation, Vector((-0.60, 0.45, -0.65)), Hr)

    # The left fist takes the same grip, LEFT_UP further along the blade. Both
    # hands on a two-hander point the same way, so the rotation carries over.
    blade_up_world = (M.to_3x3() @ Vector(up)).normalized()
    Hl = Matrix.Translation(Hr.translation + blade_up_world * LEFT_UP) @ Hr.to_3x3().to_4x4()
    l = ik_arm("Left", Hl.translation, Vector((0.60, 0.45, -0.65)), Hl)
    return {"right": r, "left": l, "blade_up": [round(v, 3) for v in blade_up_world]}


def capture_relative():
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


def sample(base_action, base_slot, frames, pose_fn):
    """Play the base clip and lay the guard over the arms, frame by frame."""
    arm = _arm()
    ad = arm.animation_data or arm.animation_data_create()
    ad.action = base_action
    if base_slot is not None:
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
    act.use_fake_user = True
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
    return act, slot


def holder(name, action, slot):
    """A bare armature object carrying one clip, which is what merge sees.

    Shares `Armature`'s data rather than duplicating it: merge_animations only
    ever reads the ACTION off these, and a second copy of a 69-bone rig per clip
    would be a megabyte each of nothing.
    """
    old = bpy.data.objects.get(name)
    if old:
        bpy.data.objects.remove(old, do_unlink=True)
    obj = bpy.data.objects.new(name, _arm().data)
    bpy.context.scene.collection.objects.link(obj)
    ad = obj.animation_data_create()
    ad.action = action
    ad.action_slot = slot
    return obj


def base_of(clip):
    src = bpy.data.objects[SOURCES[clip]]
    ad = src.animation_data
    return ad.action, ad.action_slot


def hips_travel(action, slot, frames):
    arm = _arm()
    ad = arm.animation_data or arm.animation_data_create()
    ad.action = action
    ad.action_slot = slot
    pts = []
    for f in frames:
        bpy.context.scene.frame_set(f)
        bpy.context.view_layer.update()
        pts.append((arm.matrix_world @ arm.pose.bones["mixamorig:Hips"].matrix).translation.copy())
    return pts[-1] - pts[0]


def main():
    report = {"pose": build_guard()}
    rel_hold = capture_relative()

    reset_pose()
    build_guard(WRIST_WORLD + RECOIL_OFFSET, RECOIL_TILT_DEG)
    rel_recoil = capture_relative()

    for clip in ("Guard", "GuardWalk", "GuardImpact"):
        act, slot = base_of(clip)
        f0, f1 = (int(v) for v in act.frame_range)
        frames = list(range(f0, f1 + 1))

        if clip == "Guard":
            def pose_fn(i):
                return rel_hold, min(1.0, i / float(GUARD_RAISE))
        elif clip == "GuardWalk":
            def pose_fn(i):
                return rel_hold, 1.0
        else:
            def pose_fn(i, n=len(frames)):
                if i <= RECOIL_PEAK:
                    t = i / float(max(RECOIL_PEAK, 1))
                    return {k: blend(rel_hold[k], rel_recoil[k], t) for k in rel_hold}, 1.0
                t = min(1.0, (i - RECOIL_PEAK) / float(max(RECOIL_SETTLE - RECOIL_PEAK, 1)))
                return {k: blend(rel_recoil[k], rel_hold[k], t) for k in rel_hold}, 1.0

        snaps = sample(act, slot, frames, pose_fn)
        new_act, new_slot = write_action(clip, snaps, frames)
        holder(clip, new_act, new_slot)
        report[clip] = {"frames": len(frames), "base": SOURCES[clip]}

        if clip == "GuardWalk":
            # The runtime reads a guard-walk's displacement from its root motion,
            # so it has to travel exactly what Walk travels.
            was = hips_travel(act, slot, frames)
            now = hips_travel(new_act, new_slot, frames)
            drift = (now - was).length
            if drift > 1e-4:
                raise RuntimeError("GuardWalk travel drifted %.6f m from Walk" % drift)
            report[clip]["travel_drift"] = round(drift, 8)

    _arm().animation_data.action = None
    print("MAKE_MINIBOSS_GUARD " + repr(report))
    return report


if __name__ == "__main__":
    main()
    if "--save" in sys.argv:
        bpy.ops.wm.save_mainfile()
        print("SAVED " + bpy.data.filepath)
