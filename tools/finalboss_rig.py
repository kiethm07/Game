"""Pose-authoring primitives for the final boss's 37-bone Mutant rig.

Imported by the make_finalboss_*.py scripts; does nothing on its own.

    import sys, os
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import finalboss_rig as rig

Why a module and not a fourth copy of the same 200 lines
--------------------------------------------------------
tools/make_katana_block.py and tools/make_miniboss_guard.py each grew their own
copy of `aim_matrix` / `set_bone_world` / `two_bone_ik`, because each was written
for one character. The final boss needs the same solver in five separate scripts
(guard, flinch, break, death, attacks), so it is factored out once here. The two
older scripts are left alone: they are working, shipped, and tuned against their
own rigs, and rewriting them to import this would be a change with no product on
the other side of it.

What is different about THIS rig
--------------------------------
Measured off finalboss.blend, not assumed:

  * 37 bones, not Mixamo's usual 65. There are no left-hand finger bones at all
    and no spine bones above Spine2 beyond Neck/Head -- so any loop that expects
    a symmetric hand hierarchy will find one side missing. FINGERS lists what
    actually exists.
  * The arms are SHORT relative to the shoulders: 0.264 + 0.268 = 0.532 m of
    reach from a shoulder joint sitting 0.274 m off the midline. That single
    number is why the crossed-arm guard cannot put each fist past the opposite
    shoulder the way the reference photo does, and why the kneel has to pitch
    the spine ~55 deg before a fist can touch the floor. Every target in the
    authoring scripts is checked against it -- ik_arm RAISES rather than
    silently straightening the elbow and lying about the pose.
  * The two arms are not the same length. LeftHand is 0.268 and RightHand is
    0.162: the left is the Mutant's oversized arm. Poses that read as
    "the big arm" mean the LEFT one.
  * The object sits at 0.01 scale under a 90 deg X rotation, exactly as Mixamo
    exports it, so armature space is CENTIMETRES. set_bone_world exists to keep
    that out of every call site.

Axes, measured off the walk clip's own travel rather than assumed:
    forward = -Y, left = +X, up = +Z.
"""

import math

import bpy
from mathutils import Matrix, Quaternion, Vector

MAIN_ARM = "Armature"
HIPS = "mixamorig:Hips"
CHEST = "mixamorig:Spine2"

FORWARD = Vector((0.0, -1.0, 0.0))
LEFT = Vector((1.0, 0.0, 0.0))
UP = Vector((0.0, 0.0, 1.0))

SPINE = ["mixamorig:Spine", "mixamorig:Spine1", "mixamorig:Spine2"]
NECK = ["mixamorig:Neck", "mixamorig:Head"]

# Parent-first in both cases: setting a parent's matrix moves its children, so a
# chain has to be written from the root down or the later writes are undone.
ARM_BONES = [
    "mixamorig:RightShoulder", "mixamorig:RightArm",
    "mixamorig:RightForeArm", "mixamorig:RightHand",
    "mixamorig:LeftShoulder", "mixamorig:LeftArm",
    "mixamorig:LeftForeArm", "mixamorig:LeftHand",
]
LEG_BONES = [
    "mixamorig:LeftUpLeg", "mixamorig:LeftLeg",
    "mixamorig:LeftFoot", "mixamorig:LeftToeBase",
    "mixamorig:RightUpLeg", "mixamorig:RightLeg",
    "mixamorig:RightFoot", "mixamorig:RightToeBase",
]

# Only the right hand has any. Listed so a fist can be closed without a loop
# that assumes the left one has fingers too and silently poses nothing.
FINGERS = ["Index", "Pinky", "Thumb"]

# How far the mass a bone DRIVES actually extends past that bone's head, in
# metres along its own axis. Measured off the vertex weights, not off the
# skeleton -- and on this character the two are nothing like each other.
#
# The Mutant has no `mixamorig:LeftHand` vertex group at all. Its oversized left
# arm is one rigid club weighted entirely to `mixamorig:LeftForeArm`, running
# 0.953 m along a 0.268 m bone with a 0.376 m radius. So the LeftHand bone
# deforms NOTHING: posing it moves no vertices, and the visible left fist is
# wherever the FOREARM points, 0.95 m out. Solving that arm to put its hand bone
# somewhere puts the fist a metre past where you asked -- measured, before this
# table existed: a guard aimed to cross the chest threw the club over the head
# to z 2.06, and a kneel aimed to plant the fist buried 0.25 m of arm under the
# floor. Both looked correct in joint positions, because joints have no volume.
#
# The right arm is ordinary: forearm, hand and fingers each drive their own
# share. Only the left is a club.
LIMB_TIP = {
    "mixamorig:LeftForeArm": 0.953,     # elbow -> left fist tip (no hand group)
    "mixamorig:RightForeArm": 0.321,
    "mixamorig:RightHand": 0.214,       # wrist -> knuckles
    "mixamorig:LeftFoot": 0.236,
    "mixamorig:RightFoot": 0.238,
}

# Shoulder joint -> visible fist tip, per side. The asymmetry is the character:
# the left arm reaches half again as far as the right.
ARM_TIP = {
    "Left": 0.264 + LIMB_TIP["mixamorig:LeftForeArm"],    # 1.217
    "Right": 0.264 + 0.268 + LIMB_TIP["mixamorig:RightHand"],  # 0.746
}

# Arm reach from the shoulder JOINT (Arm head), i.e. upper + forearm, in METRES.
# Measured; see the module docstring. Quoted in metres because that is the unit
# every target in the authoring scripts is written in.
ARM_REACH = 0.532

# How much of a chain must be left unspent, as a FRACTION of its own length: a
# solve allowed to consume the last millimetre gives a locked joint, which reads
# as a mannequin. A fraction and not an absolute distance on purpose -- the
# solver works in armature space, which is CENTIMETRES here (the rig sits at
# 0.01 scale), so an absolute margin written in metres would silently be a
# hundred times smaller than intended and never fire.
ELBOW_MARGIN_FRAC = 0.04


def arm():
    return bpy.data.objects[MAIN_ARM]


def scene_fps():
    scn = bpy.context.scene
    return scn.render.fps / scn.render.fps_base


# --------------------------------------------------------------------------
# posing
# --------------------------------------------------------------------------

def reset_pose():
    for pb in arm().pose.bones:
        pb.matrix_basis = Matrix.Identity(4)
    bpy.context.view_layer.update()


def aim_matrix(head, direction, ref_x):
    """Armature-space bone matrix with +Y along `direction`, X kept near `ref_x`.

    Blender bones point down their own local +Y, so aiming a bone is choosing
    that axis; `ref_x` pins the remaining roll to whatever the rest pose used,
    which is what stops a solved arm from spiralling about its own length
    between one frame and the next.
    """
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


def rest_x(bone_name):
    """The bone's rest roll axis, for feeding to aim_matrix as `ref_x`."""
    return Vector(arm().data.bones[bone_name].matrix_local.col[0][:3])


def _rest_basis(bone_name):
    """The bone's rest orientation in armature space, columns normalised."""
    m = arm().data.bones[bone_name].matrix_local.to_3x3()
    for i in range(3):
        c = Vector((m[0][i], m[1][i], m[2][i])).normalized()
        m[0][i], m[1][i], m[2][i] = c.x, c.y, c.z
    return m


def swing_matrix(bone_name, head_a, dir_a):
    """Armature-space matrix aiming a bone along `dir_a` with NO twist vs rest.

    This is the right way to point a LIMB, and aim_matrix is not.

    aim_matrix pins the roll with a reference axis, and picks an arbitrary one
    whenever the target direction runs near-parallel to that reference. On this
    rig that is not a corner case: the arms lie along +-X in the rest T-pose, so
    a forearm aimed straight DOWN -- which is exactly what a fist planted on the
    floor asks for -- lands in the degenerate branch and the roll comes out of
    nowhere. Measured, before this function existed: the Mutant's oversized left
    forearm span-rotated about its own length and put 0.27 m of arm mesh under
    the floor while every joint position still read as correct, because joints
    have no roll to be wrong about.

    A minimal (swing-only) rotation from the bone's rest direction to the target
    has no such freedom to get wrong. The roll is whatever the rest pose had,
    which for a skinned limb is the only roll that means anything.
    """
    basis = _rest_basis(bone_name)
    rest = (basis @ Vector((0.0, 1.0, 0.0))).normalized()
    q = rest.rotation_difference(Vector(dir_a).normalized())
    m = (q.to_matrix() @ basis).to_4x4()
    m.translation = Vector(head_a)
    return m


def set_bone_swing(bone_name, head_a, dir_a):
    """Aim a bone along an ARMATURE-space direction, twist-free."""
    arm().pose.bones[bone_name].matrix = swing_matrix(bone_name, head_a, dir_a)
    bpy.context.view_layer.update()


def to_armature(world_point):
    return arm().matrix_world.inverted() @ Vector(world_point)


def to_world(armature_point):
    return arm().matrix_world @ Vector(armature_point)


def set_bone_world(name, world_mat):
    """Set a bone from a WORLD matrix, dropping the armature's 0.01 object scale.

    The rig sits at scale 0.01, so armature space is centimetres; converting a
    world matrix through matrix_world.inverted() drags a 100x into the rotation
    block, which has to be renormalised away or the bone inherits the scale.
    """
    a = arm()
    am = a.matrix_world.inverted() @ world_mat
    loc = am.translation.copy()
    r = am.to_3x3()
    for i in range(3):
        c = Vector((r[0][i], r[1][i], r[2][i])).normalized()
        r[0][i], r[1][i], r[2][i] = c.x, c.y, c.z
    m = r.to_4x4()
    m.translation = loc
    a.pose.bones[name].matrix = m
    bpy.context.view_layer.update()


def bone_world(name):
    """A pose bone's CURRENT world matrix."""
    return arm().matrix_world @ arm().pose.bones[name].matrix


def two_bone_ik(upper, fore, target_a, pole_a, l2_override=None):
    """Elbow/knee position for a chain reaching `target_a`, in armature space.

    Analytic, not iterative: the joint lies on the circle where the two spheres
    of radius l1 and l2 intersect, and `pole_a` picks the point on that circle.
    Read off the chain's CURRENT head, so a solve run after the shoulder has
    been aimed starts from where the shoulder actually left it.
    """
    a = arm()
    start = a.pose.bones[upper].matrix.translation.copy()
    l1 = a.data.bones[upper].length
    # `l2_override` lets a chain be solved to the tip of the MASS the second
    # bone drives rather than to the bone's own tail -- which is the only way to
    # aim the left club, whose bone is 0.268 and whose fist is 0.953 out.
    l2 = a.data.bones[fore].length if l2_override is None else l2_override
    v = Vector(target_a) - start
    d = max(abs(l1 - l2) + 1e-4, min(l1 + l2 - 1e-4, v.length))
    axis = v.normalized()
    x = (l1 * l1 - l2 * l2 + d * d) / (2 * d)
    h = math.sqrt(max(l1 * l1 - x * x, 0.0))
    p = Vector(pole_a)
    p = p - axis * p.dot(axis)
    if p.length < 1e-6:
        p = Vector((0, 0, 1)) - axis * axis.z
    p.normalize()
    return start, start + axis * x + p * h, d, l1 + l2


def ik_chain(upper, fore, end, target_world, pole_world, end_dir_world=None,
             label="", l2_override=None):
    """Solve `upper`->`fore` to put `fore`'s tail on `target_world`.

    `end_dir_world`, when given, is the world direction the terminal bone (hand
    or foot) is then aimed along; without it the terminal bone keeps whatever
    the chain left it with. Returns the reach actually used against the chain's
    limit, so a caller can assert on how close to straight the joint ended up.

    Every bone is set with swing_matrix, so no step in the chain invents a twist
    -- see the note there for what that cost before it was fixed.
    """
    a = arm()
    inv = a.matrix_world.inverted()
    target_a = inv @ Vector(target_world)
    pole_a = (inv.to_3x3() @ Vector(pole_world)).normalized()

    # `l2_override` arrives in METRES like every other public length here; the
    # solver runs in armature units, which are centimetres on this rig.
    scale = arm().matrix_world.to_scale().x
    l2 = None if l2_override is None else l2_override / scale
    start, joint, d, limit = two_bone_ik(upper, fore, target_a, pole_a, l2)
    # Both in armature units (centimetres), so the comparison is unit-free.
    if d > limit * (1.0 - ELBOW_MARGIN_FRAC):
        # The chain's START is in the message on purpose. Without it the only
        # way to tell a target that is genuinely too far from a target the
        # torso moved out from under is to re-derive the joint by hand.
        raise RuntimeError(
            "%s cannot reach %s from %s: %.3f m needed of a %.3f m chain "
            "(%.0f%% extended, limit %.0f%%). Move the target in, or pitch the "
            "body toward it -- straightening the joint to fake the reach is "
            "what makes a pose read as a mannequin."
            % (label or upper, tuple(round(v, 3) for v in target_world),
               tuple(round(v, 3) for v in (arm().matrix_world @ start)),
               (target_a - start).length * scale, limit * scale,
               100.0 * d / limit, 100.0 * (1.0 - ELBOW_MARGIN_FRAC)))

    set_bone_swing(upper, start, joint - start)
    mid = a.pose.bones[fore].matrix.translation.copy()
    set_bone_swing(fore, mid, target_a - mid)
    if end_dir_world is not None:
        dir_a = inv.to_3x3() @ Vector(end_dir_world)
        tip = a.pose.bones[end].matrix.translation.copy()
        set_bone_swing(end, tip, dir_a)
    # Reported in METRES, not the centimetres the solve ran in, so the numbers
    # can be read against the targets that produced them. `extended` is the one
    # to look at: 100% is a locked joint, and a natural guard sits nearer 70.
    scale = arm().matrix_world.to_scale().x
    return {"reach_m": round(d * scale, 3), "limit_m": round(limit * scale, 3),
            "extended_pct": round(100.0 * d / limit, 1)}


def ik_arm(side, target_world, pole_world, hand_dir_world=None, target="fist"):
    """Aim `side`'s arm so its visible FIST lands on `target_world`.

    The fist and the wrist are not the same place on either arm -- see LIMB_TIP
    -- and they are differently not-the-same on each. `target` says which one
    the caller means, and defaults to the fist because that is the thing anyone
    posing a character is actually thinking about.

    Left: the chain is solved with the club's 0.953 m as its second segment, so
    what arrives at the target is the mass a viewer sees. There is no wrist to
    aim at, because the left hand bone drives no vertices.

    Right: an ordinary arm. A "fist" target is converted to the wrist by
    stepping back 0.214 m along `hand_dir_world` (which is therefore required);
    "wrist" places the forearm's tail directly, for poses like a hand laid on a
    knee where the wrist is the contact.
    """
    if target not in ("fist", "wrist"):
        raise ValueError("target must be 'fist' or 'wrist', not %r" % target)
    names = ["mixamorig:%s%s" % (side, s) for s in ("Arm", "ForeArm", "Hand")]
    if side == "Right" and target == "fist":
        if hand_dir_world is None:
            raise ValueError(
                "a fist target on the right arm needs hand_dir_world: the "
                "knuckles are 0.214 m past the wrist and the direction is what "
                "says which way.")
        step = Vector(hand_dir_world).normalized() * LIMB_TIP["mixamorig:RightHand"]
        target_world = Vector(target_world) - step
    if side == "Left":
        # The left hand bone drives no vertices, so it is aimed along the club
        # purely so the skeleton reads sensibly in the viewport.
        res = ik_chain(names[0], names[1], names[2], target_world, pole_world,
                       None, label="%s arm" % side.lower(),
                       l2_override=LIMB_TIP["mixamorig:LeftForeArm"])
        elbow = bone_world(names[1]).translation
        set_bone_swing(names[2], arm().pose.bones[names[2]].matrix.translation,
                       to_armature(target_world) - to_armature(elbow))
        res["fist_at"] = [round(v, 3) for v in fist_world(side)]
        return res
    res = ik_chain(names[0], names[1], names[2], target_world, pole_world,
                   hand_dir_world, label="%s arm" % side.lower())
    res["fist_at"] = [round(v, 3) for v in fist_world(side)]
    return res


def fist_world(side):
    """Where the visible fist actually is, in world metres.

    Left: the club tip, measured out along the FOREARM. Right: the knuckles,
    measured out along the hand. Neither is the hand bone's own position.
    """
    if side == "Left":
        m = bone_world("mixamorig:LeftForeArm")
        reach = LIMB_TIP["mixamorig:LeftForeArm"]
    else:
        m = bone_world("mixamorig:RightHand")
        reach = LIMB_TIP["mixamorig:RightHand"]
    # bone_world carries the rig's 0.01 object scale in its rotation block, so
    # the axis has to be normalised before a distance in metres is stepped
    # along it. The translation is already in metres.
    axis = (m.to_3x3() @ Vector((0.0, 1.0, 0.0))).normalized()
    return m.translation + axis * reach


def ik_leg(side, ankle_world, pole_world, foot_dir_world=None):
    """Put `side`'s ankle on `ankle_world`, optionally aiming the foot."""
    names = ["mixamorig:%s%s" % (side, s) for s in ("UpLeg", "Leg", "Foot")]
    return ik_chain(names[0], names[1], names[2], ankle_world, pole_world,
                    foot_dir_world, label="%s leg" % side.lower())


def aim_bone(name, toward_world):
    """Rotate a bone in place so it points at a world position, twist-free."""
    a = arm()
    head = a.pose.bones[name].matrix.translation.copy()
    tgt = a.matrix_world.inverted() @ Vector(toward_world)
    set_bone_swing(name, head, tgt - head)


def rotate_bone_world(name, deg, axis_world, pivot_world):
    """Rotate a bone about a WORLD axis through a WORLD pivot; children follow.

    Preferred over a local-space rotation everywhere the intent is anatomical
    ("lean back 20 degrees"). A Mixamo bone's local axes are whatever the export
    happened to roll them to, so a local rotation about "X" means something
    different on the spine than on the neck, and the sign has to be discovered
    by trial. In world terms the rig faces -Y with +X to its left, so a POSITIVE
    rotation about +X tips the body forward and a negative one leans it back --
    true for every bone, no discovery needed.
    """
    m = bone_world(name)
    p = Vector(pivot_world)
    # Matrix.Rotation takes either an axis letter or a vector; pass whichever
    # the caller used straight through rather than coercing "X" into a Vector.
    axis = axis_world if isinstance(axis_world, str) else Vector(axis_world)
    r = (Matrix.Translation(p)
         @ Matrix.Rotation(math.radians(deg), 4, axis)
         @ Matrix.Translation(-p))
    set_bone_world(name, r @ m)


def offset_bone_world(name, delta):
    """Translate a bone in world metres, carrying its children with it."""
    m = bone_world(name).copy()
    m.translation = m.translation + Vector(delta)
    set_bone_world(name, m)


def curl_chain(bones, total_deg, axis_world, weights=None):
    """Spread a world-space bend across a chain, each bone about its own head.

    Putting the whole rotation on one joint gives a hinge; splitting it across
    Spine/Spine1/Spine2 gives a curve. Because each bone carries its children,
    the shares accumulate: the top of the chain ends up rotated by the full
    `total_deg`. `weights` defaults to even.
    """
    w = weights or [1.0] * len(bones)
    total = float(sum(w))
    for bone, share in zip(bones, w):
        head = bone_world(bone).translation.copy()
        rotate_bone_world(bone, total_deg * share / total, axis_world, head)


def close_fist(side, curl_deg=75.0):
    """Curl whatever finger bones this side actually has.

    The left hand has none -- it is modelled as one solid mitt -- so calling
    this for "Left" is a no-op by design rather than an error.
    """
    a = arm()
    n = 0
    for finger in FINGERS:
        for seg in (1, 2, 3):
            name = "mixamorig:%sHand%s%d" % (side, finger, seg)
            pb = a.pose.bones.get(name)
            if pb is None:
                continue
            deg = curl_deg * (0.6 if finger == "Thumb" else 1.0)
            pb.matrix_basis = pb.matrix_basis @ Matrix.Rotation(
                math.radians(-deg), 4, "Z")
            n += 1
    bpy.context.view_layer.update()
    return n


# --------------------------------------------------------------------------
# blending and capture
# --------------------------------------------------------------------------

def blend(a, b, t):
    if t <= 0:
        return a.copy()
    if t >= 1:
        return b.copy()
    la, qa, sa = a.decompose()
    lb, qb, sb = b.decompose()
    return Matrix.LocRotScale(la.lerp(lb, t), qa.slerp(qb, t), sa.lerp(sb, t))


def ease(t):
    """Smoothstep. Linear keys on a solved pose read mechanical at the ends."""
    t = max(0.0, min(1.0, t))
    return t * t * (3.0 - 2.0 * t)


def capture_relative(bones, ref=CHEST):
    """Capture `bones` against `ref`, so the pose can ride a moving base clip.

    Stored in absolute armature space instead, an arm pose would hang fixed in
    the air while the body walked out from under it -- which is the whole
    reason the guard is captured this way and not as local rotations.
    """
    a = arm()
    r = a.pose.bones[ref].matrix.copy()
    return {b: r.inverted() @ a.pose.bones[b].matrix.copy() for b in bones}


def capture_basis(bones=None):
    """Capture bones as LOCAL basis matrices -- the right thing to interpolate.

    Blending two poses through their absolute armature-space matrices lerps each
    joint's world position independently, which does not preserve bone length:
    the skeleton stretches and folds through shapes neither end pose contains.
    Measured on the standing->kneel transition before this existed, the body
    passed 0.35 m through the floor mid-blend and had to be dragged back up
    afterwards.

    A local basis is a rotation about a joint (plus the hips' own translation),
    so slerping it swings each limb along an arc of the correct length. Every
    intermediate frame is a pose the skeleton can actually hold.
    """
    a = arm()
    names = bones or [pb.name for pb in a.pose.bones]
    return {b: a.pose.bones[b].matrix_basis.copy() for b in names}


def apply_basis(pose, weight=1.0):
    """Blend the rig toward a capture_basis() pose. Order-independent."""
    a = arm()
    for bone, m in pose.items():
        pb = a.pose.bones[bone]
        pb.matrix_basis = blend(pb.matrix_basis, m, weight)
    bpy.context.view_layer.update()


def mirror_name(bone):
    """The bone on the other side, or the bone itself for midline bones."""
    if bone.startswith("mixamorig:Left"):
        return "mixamorig:Right" + bone[len("mixamorig:Left"):]
    if bone.startswith("mixamorig:Right"):
        return "mixamorig:Left" + bone[len("mixamorig:Right"):]
    return bone


def mirror_basis(pose):
    """Reflect a capture_basis() pose left-to-right.

    Each bone takes its partner's local rotation with the y and z components of
    the quaternion negated, and its location's x negated -- Blender's own
    "paste flipped pose" convention. It is only valid because this rig's rest
    pose IS mirror-symmetric: measured, the shoulder, elbow, wrist, hip, knee
    and ankle joints all sit at matching +-X with identical y and z.

    What is NOT symmetric is the SKIN. The left arm is a 0.953 m club and the
    right is an ordinary arm, and only the right has fingers. So a mirrored
    swing is the same MOTION performed by the other limb, which is exactly what
    alternating swipes need -- not a mirror image of the character.

    Bones whose partner is missing (the right hand's fingers) keep their own
    rotation rather than being dropped, so a mirrored pose still has a fist.
    """
    out = {}
    for bone in pose:
        partner = mirror_name(bone)
        source = pose.get(partner, pose[bone])
        loc, quat, scale = source.decompose()
        flipped = Quaternion((quat.w, quat.x, -quat.y, -quat.z))
        out[bone] = Matrix.LocRotScale(
            Vector((-loc.x, loc.y, loc.z)), flipped, scale)
    return out


def capture_absolute(bones=None):
    """Capture bones as raw pose matrices, for poses that own the whole body."""
    a = arm()
    names = bones or [pb.name for pb in a.pose.bones]
    return {b: a.pose.bones[b].matrix.copy() for b in names}


def apply_relative(rel, weight=1.0, ref=CHEST):
    """Re-apply a capture_relative() pose against the CURRENT `ref`."""
    a = arm()
    r = a.pose.bones[ref].matrix.copy()
    for bone, m in rel.items():
        was = a.pose.bones[bone].matrix.copy()
        a.pose.bones[bone].matrix = blend(was, r @ m, weight)
        bpy.context.view_layer.update()


def apply_absolute(pose, weight=1.0):
    a = arm()
    for bone, m in pose.items():
        was = a.pose.bones[bone].matrix.copy()
        a.pose.bones[bone].matrix = blend(was, m, weight)
        bpy.context.view_layer.update()


# --------------------------------------------------------------------------
# clip in / clip out
# --------------------------------------------------------------------------

def fcurves(action):
    """Every fcurve of a slotted (4.4+) or legacy action."""
    if hasattr(action, "fcurves"):
        return list(action.fcurves)
    out = []
    for layer in action.layers:
        for strip in layer.strips:
            for bag in strip.channelbags:
                out.extend(bag.fcurves)
    return out


def source_of(clip):
    """The action and slot carried by the holder armature named `clip`."""
    obj = bpy.data.objects.get(clip)
    if obj is None:
        raise SystemExit(
            "no clip armature named %r. Run tools/build_finalboss_pack.py "
            "first -- that is what imports the source FBXs." % clip)
    ad = obj.animation_data
    if not (ad and ad.action):
        raise SystemExit("%r carries no action" % clip)
    return ad.action, ad.action_slot


def play(action, slot, frame):
    """Drive the main armature to one frame of a source clip.

    `frame` may be fractional. Blender evaluates the subframe for real, so a
    clip can be resampled at any rate without the caller interpolating poses by
    hand -- which is how the attack flurries run their source swing faster than
    it was authored.
    """
    a = arm()
    ad = a.animation_data or a.animation_data_create()
    ad.action = action
    if slot is not None:
        ad.action_slot = slot
    whole = math.floor(frame)
    bpy.context.scene.frame_set(int(whole), subframe=float(frame) - whole)
    bpy.context.view_layer.update()


def detach():
    a = arm()
    if a.animation_data:
        a.animation_data.action = None


def snapshot():
    return {pb.name: pb.matrix_basis.decompose() for pb in arm().pose.bones}


def write_action(name, snaps, frames):
    """Key every bone on every frame, so the clip stands alone.

    Deliberately not sparse. These clips are solved rather than authored, so
    there is no meaningful keyframe to reduce to -- and merge_animations exports
    with export_force_sampling anyway, which would resample whatever it was
    given. Constant channels are dropped at export by
    export_optimize_animation_size.
    """
    a = arm()
    old = bpy.data.actions.get(name)
    if old:
        bpy.data.actions.remove(old)
    act = bpy.data.actions.new(name)
    act.use_fake_user = True
    slot = act.slots.new(id_type="OBJECT", name="Armature")
    bag = act.layers.new("Layer").strips.new(type="KEYFRAME").channelbags.new(slot)
    for pb in a.pose.bones:
        stem = 'pose.bones["%s"].' % pb.name
        for path, size, part in (("location", 3, 0),
                                 ("rotation_quaternion", 4, 1),
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
    ever reads the ACTION off these, and a second copy of the rig per clip would
    be dead weight in the .blend.
    """
    old = bpy.data.objects.get(name)
    if old:
        bpy.data.objects.remove(old, do_unlink=True)
    obj = bpy.data.objects.new(name, arm().data)
    bpy.context.scene.collection.objects.link(obj)
    ad = obj.animation_data_create()
    ad.action = action
    ad.action_slot = slot
    return obj


def bake(name, count, pose_fn):
    """Run `pose_fn(i)` for i in 0..count-1 and store the result as clip `name`.

    `pose_fn` leaves the rig posed for that frame however it likes -- playing a
    base clip, solving IK, or both. Everything the rig is holding when it
    returns is what gets keyed.
    """
    frames = list(range(1, count + 1))
    snaps = []
    for i in range(count):
        pose_fn(i)
        bpy.context.view_layer.update()
        snaps.append(snapshot())
    act, slot = write_action(name, snaps, frames)
    holder(name, act, slot)
    detach()
    return {"clip": name, "frames": count,
            "seconds": round(count / scene_fps(), 3)}


# --------------------------------------------------------------------------
# measurement
# --------------------------------------------------------------------------

def joint_track(clip, bones):
    """World positions of `bones` across every frame of `clip`.

    The only honest way to check an authored clip: a pose that looks right in
    one solved frame can still drag a foot through the floor on frame 40.
    """
    act, slot = source_of(clip)
    f0, f1 = (int(v) for v in act.frame_range)
    out = {b: [] for b in bones}
    for f in range(f0, f1 + 1):
        play(act, slot, f)
        for b in bones:
            out[b].append(bone_world(b).translation.copy())
    detach()
    return out


def hips_travel(clip):
    track = joint_track(clip, [HIPS])[HIPS]
    return track[-1] - track[0]
