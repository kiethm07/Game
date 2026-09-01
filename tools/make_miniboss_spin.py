"""Author the mini boss's spinning greatsword attack -- `Attack_Spin`.

    blender -b ~/Documents/3D/Model/pack_miniboss.blend \
        --python tools/make_miniboss_spin.py -- --save

Run AFTER tools/build_miniboss_pack.py and tools/make_miniboss_guard.py, and
re-run it after any rebuild of the pack: like the guard set, this clip is
AUTHORED here rather than downloaded, so build_miniboss_pack's strip_old_clips()
would throw it away.

What it is
----------
A committed, telegraphed special: the boss winds the greatsword back in both
hands, HOLDS that wound pose for half a second, then turns three full circles
carrying the blade around at chest height, and settles. It replaces the single
horizontal cut (`Attack_H`) in the rotation.

Where the motion comes from
---------------------------
`standing melee attack 360 high.fbx` -- the 360 in Mixamo's melee pack -- is the
reference for the SPIN, and it is used for the body: the pivot, the cross-step
and the lean are its footwork, not invented here. What it is not used for is the
arms. Measured on this character (the tip carried through the joint hierarchy,
same method as every other window in AttackRegistry):

  * the clip is authored for a one-handed weapon and rests the blade over the
    shoulder for its first 30 frames -- the tip sits 2.0-2.8 m UP, pointing
    backwards, and never crosses the front at all until frame 31
  * its one low pass, frames 43-52, drags the tip to -0.21 m, a quarter of a
    metre UNDER the floor, because nothing in it knows the blade grew to 2.48 m

Both are the same fact: a 2.48 m two-hander swung by an animation authored for
an arming sword goes wherever the wrist goes, and that is not a spin attack.

So the SWORD ARM is solved instead, exactly as tools/make_miniboss_guard.py
solves the guard, and for the same reason -- the sword is rigid-bound to
`mixamorig:RightHand`, so the honest way to pose it is backwards: choose the
world transform the SWORD should have, divide out the bind offset for the wrist
that produces it, then run analytic two-bone IK up the arm.

The RIGHT arm only. The carry is one-handed, so the left arm is left to the
source clip and swings with the turn as a counterweight, which is what it is
there for; nothing is solved onto it and its fingers are not curled onto a hilt
it is not holding.

Two holds are authored, and they are held two different ways:

  WIND   sword hand cocked back past the right hip, blade trailing and rising.
         Captured against `mixamorig:Spine2` and re-applied over the chest, the
         way make_miniboss_guard holds a guard, so the cocked pose rides the
         body's own breathing rather than hanging in space.
  SWEEP  blade out level at chest height, arms locked. Re-SOLVED in world space
         on every frame from the body's heading alone -- see SWEEP_WRIST for the
         measurement that forced it. Riding the chest carries its pitch and roll
         as well as its turn, and this clip's pivot leans hard.

The timeline
------------
30 fps, the source's own rate. Output frames are after the cross-fades, which
overlap the phases rather than lengthening them:

    WINDUP    12f  0.40s  source 1-7 at half speed: the draw-back
    HOLD       9f  0.30s  source 7-8, all but frozen: the telegraph
    SPIN    3x20f  2.00s  source 8-47 resampled, three times
    RECOVER   19f  0.63s  source 48-80: the settle
                  ------
                   3.33s  100 frames

The telegraph is the wind and the hold together and is 0.90s long. What the hit
windows are timed against, though, is not that but the three passes the blade
makes across the FRONT of the character: front_passes() reports them, one per
revolution, and AttackRegistry gives each one window. The sweep behind and
beside the boss is picture -- only the pass in front can hit.

SPIN_SEGMENT is the source's fast pivot with its slow entry and its settle cut
off. It does not loop -- measured, no pair of frames in this clip agree to
better than 24 deg RMS over the legs and spine once their yaw difference is
divided out, so there is no seam to find and CROSSFADE hides the one we take.

Which way it turns
------------------
Anticlockwise seen from above, and the source clip turns the other way. What
reverses it is a left-right MIRROR of the body (see MIRROR), not a negated yaw:
the turn is the source's own footwork, and spinning the hips backwards would
leave the feet pivoting into a turn that is no longer happening.

Two things follow from the mirror, and both are handled rather than lived with:

  * the body's COIL reverses with the turn, and the source clip does not open
    square -- its frame 1 already has the hips 50 deg round. So the clip is
    turned bodily until the hold faces the way the entity faces (FACE_FRAME),
    which is both what a wind-up should look like and what puts the cocked blade
    back behind the right shoulder where it was before the mirror.
  * squared up that way, the pass across the front lands 0.52 s into each
    revolution, and the third one ran out of turn 0.02 s before it finished
    crossing. SPIN_OVERSHOOT carries the last revolution far enough to finish
    it, and the recovery unwinds the overshoot again.

Its yaw is RESCALED, not replaced. The segment turns 332.5 deg of the 360 a
revolution needs, so every frame's rotation is multiplied by 360/332.5 = 1.083
about the vertical through the hips. Forcing a constant rate instead would flatten
the clip's own wind-and-release rhythm and slide the feet by the full 27 deg the
segment is short; scaling spreads the same correction over the whole turn as 8%,
and lands each revolution exactly 360 deg on from the last, so the seam carries
no snap and three revolutions are three revolutions.

The hips are PINNED horizontally at the rest position throughout. The attack is
in place -- it does not use root motion, because the closing is done by the
runtime instead (AttackData::getAdvanceSpeed, so the boss tracks a player who
runs rather than committing to a direction at the windup) -- and a spin that
wanders is a spin that leaves its own collision capsule.
"""

import math
import os
import sys

import bpy
from mathutils import Matrix, Quaternion, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import make_miniboss_guard as guard

MAIN_ARM = "Armature"
HIPS = "mixamorig:Hips"
CLIP = "Attack_Spin"
SCRATCH = "src_Spin360"

SOURCE_FBX = ("/Users/long/Documents/3D/Model/animation pack mini boss/"
              "standing melee attack 360 high.fbx")

# Parent-first: setting a parent's matrix moves its children. The RIGHT arm
# only -- this is a one-handed carry, and the left arm is left to the source
# clip so it counterbalances the turn instead of being frozen onto a hilt it is
# not holding.
ARM_BONES = ["mixamorig:RightShoulder", "mixamorig:RightArm",
             "mixamorig:RightForeArm", "mixamorig:RightHand"]

# The clip the sword hand is borrowed from, and the hand it is applied to.
# `Idle` is this character holding this sword, so its finger curl is a grip
# authored for this hand. The left hand is not touched: it holds nothing.
GRIP_CLIP = "Idle"
GRIP_FRAME = 1
GRIP_SIDE = "Right"

# Timeline. (source_start, source_end, output_frames) -- the segment is sampled
# at output_frames evenly spaced points, so the ratio is the playback rate.
WINDUP = (1.0, 7.0, 12)
HOLD = (7.0, 8.0, 15)
SPIN_SEGMENT = (8.0, 47.0, 24)
SPINS = 3
RECOVER = (48.0, 80.0, 19)

# Frames of cross-fade where two segments meet. Longer at the hold -> spin join
# than inside the spin: that one is a standing pose giving way to a pivot, where
# the others are two frames of the same pivot.
CROSSFADE = 4
ENTRY_FADE = 6

# Frames of the recovery the sweep is still held through, before the arms are
# handed back to the source clip. Not zero: the third revolution's last quarter
# turn lands inside the recovery's cross-fade, and letting the hold start
# releasing there measured as a spin that stopped at 2.75 turns.
HOLD_OUT = 5

# The source clip turns CLOCKWISE seen from above; the boss turns the other way.
# Reversing it is a left-right MIRROR of the body, not a negated yaw: the turn is
# the source's own footwork -- a pivot and a cross-step -- and simply spinning
# the hips the other way would leave the feet stepping into a turn that is no
# longer happening. Mirrored, the footwork reverses with the turn and the clip
# stays as authored.
#
# Blender's own flipped-pose convention (partner bone, quaternion y and z
# negated, location x negated) is what does it, and it is only valid on a rig
# whose rest pose is symmetric. Measured on this one: 2.1 mm worst case across
# every paired joint, and a mirrored pose reproduces a true reflection of the
# original to the same 2.1 mm. mirror_pose() asserts it on every run.
#
# The two prop bones are left alone. `mixamorig:Sword_joint` and
# `mixamorig:Shield_joint` carry midline names but sit off the midline, and
# flipping them would be meaningless -- no vertex group references either, so
# they deform nothing whatever is done with them.
MIRROR = True
MIRROR_TOLERANCE = 0.01

# What the SPIN_SEGMENT's yaw is scaled up to. One revolution per repeat.
REVOLUTION = 360.0

# Extra rotation eased into the LAST revolution, unwound again by the recovery.
#
# The source clip does this itself -- measured, its own yaw runs past 360 to 416
# and settles back to 365 -- so it is that clip's follow-through rather than an
# invention. It is also what gives the third pass across the front somewhere to
# happen: squared up at the hold (see FACE_FRAME) the passes land 0.52 s into
# each revolution, and without the overshoot the third one runs out of turn
# 0.02 s before it has finished crossing.
SPIN_OVERSHOOT = 50.0

# How far into the last revolution the follow-through starts, as a fraction.
OVERSHOOT_FROM = 0.5

# How many wedges of a revolution the hitbox fan is cut into. See fan(). Kept as
# a diagnostic: the attack ships ONE window per revolution, not a fan.
SECTORS = 8

# The source frame whose heading the whole clip is turned onto, so that at that
# moment the character faces the way the ENTITY faces.
#
# It has to be a choice, because the source clip does not open square: its frame
# 1 already has the hips 50 deg round, and every heading in the clip is measured
# from there. Left alone, the boss winds up facing 50-70 deg away from the
# player it is about to attack -- and mirroring the spin to reverse it swung
# that offset the other way, which is what moved the cocked sword from behind
# the boss round to its side.
#
# The end of the HOLD is the frame to square up: it is the last one before the
# turn starts, it is the pose the player reads the attack off, and squaring it
# puts the cocked blade where the rest-frame numbers say it is -- trailing
# behind the right shoulder -- rather than wherever the clip's own coil left it.
FACE_FRAME = 8.0

# Half-width of the front, in degrees. The hit window per revolution is the time
# the tip spends inside this much of straight ahead.
#
# 50 is not a taste: it is the half-angle of the hitbox itself. AttackRegistry
# lays a bar 1.80 m either side of centre at 1.50 m forward, whose ends sit at
# atan2(1.80, 1.50) = 50.2 deg -- so a window this wide is live exactly while
# the blade is inside the arc the capsule covers, and no wider.
FRONT_HALF_DEG = 50.0

# The two holds, in world metres. This rig faces -Y, its left is +X and up is
# +Z; the character is 2.63 m, hips 1.27, shoulders 2.01, head 2.28.
#
# WIND: the sword hand cocked back past the right hip, blade trailing behind and
# rising -- the pose the reference photo holds, carried in one hand as it is
# there. The tip ends up 2.25 m up and 2.07 m behind the character.
WIND_WRIST = Vector((-0.34, 0.26, 1.66))
WIND_BLADE = Vector((-0.42, 0.86, 0.28))
# SWEEP: the sword arm out to the character's right and forward, blade level and
# leading the turn. The turn is clockwise seen from above (the source's yaw
# decreases), so the blade is carried ahead of the chest on the arm's own side --
# which is where a one-handed carry can hold it without crossing the body.
#
# Unlike the wind, this hold is NOT captured against the chest and re-applied:
# it is re-solved in WORLD space on every frame of the spin, from the body's
# heading alone. Riding the chest carries its pitch and roll as well as its
# turn, and the source's pivot leans hard -- measured, a chest-relative sweep
# put the tip through a 1.2 m bob every revolution, scraping the floor on one
# side of the turn and swinging over a standing player's head on the other.
# Taking the heading only leaves a level whirl at a height we choose, which is
# also the only version of this the hitboxes can honestly be authored against.
SWEEP_WRIST = Vector((-0.32, -0.38, 1.48))
SWEEP_BLADE = Vector((-0.72, -0.69, 0.0))
# Level, not flat: 6 deg of tip-down keeps the point inside the sweep the
# hitboxes are authored against instead of riding above a short target.
SWEEP_TILT_DEG = -6.0

# How far the blade's carry is evened out against the body's own turn, 0 to 1.
BLADE_SMOOTH = 0.65

# How far the tip sits from the wrist along the blade. Measured: the sword's
# tip is 1.836 up its own axis and the fist holds it at -0.264, so a hold puts
# the point 2.10 m past the hand whichever way it is pointed.
TIP_FROM_WRIST = 2.10

# Pole target for the elbow -- down and out, so it does not invert through the
# chest. In world metres.
POLE = {"Right": Vector((-0.70, 0.10, -0.75))}


def _arm():
    return bpy.data.objects[MAIN_ARM]


def mirror_name(bone):
    """The bone on the other side, or the bone itself for a midline one."""
    if bone.startswith("mixamorig:Left"):
        return "mixamorig:Right" + bone[len("mixamorig:Left"):]
    if bone.startswith("mixamorig:Right"):
        return "mixamorig:Left" + bone[len("mixamorig:Right"):]
    return bone


def mirror_pose():
    """Reflect the evaluated pose left to right, in place. See MIRROR."""
    arm = _arm()
    pose = {pb.name: pb.matrix_basis.copy() for pb in arm.pose.bones}
    for pb in arm.pose.bones:
        if "_joint" in pb.name:
            continue
        loc, quat, scale = pose.get(mirror_name(pb.name), pose[pb.name]).decompose()
        pb.matrix_basis = Matrix.LocRotScale(
            Vector((-loc.x, loc.y, loc.z)),
            Quaternion((quat.w, quat.x, -quat.y, -quat.z)), scale)
    bpy.context.view_layer.update()


def check_mirror(action, slot, frame):
    """Assert the flip convention really does reflect this rig.

    Poses the rig, records where every joint is, mirrors, and compares each
    joint against the reflection of its partner's recorded position. The
    convention is a shortcut for a reflection and is only worth taking while it
    IS one -- on a rig whose rest pose drifted out of symmetry it silently is
    not, and the clip would come out subtly lopsided rather than failing.
    """
    arm = _arm()
    play(action, slot, frame)
    W = arm.matrix_world
    was = {b.name: (W @ arm.pose.bones[b.name].matrix).translation.copy()
           for b in arm.data.bones}
    mirror_pose()
    worst, where = 0.0, None
    for b in arm.data.bones:
        if "_joint" in b.name:
            continue
        want = was[mirror_name(b.name)]
        now = (W @ arm.pose.bones[b.name].matrix).translation
        err = (Vector((-want.x, want.y, want.z)) - now).length
        if err > worst:
            worst, where = err, b.name
    if worst > MIRROR_TOLERANCE:
        raise RuntimeError("the flip convention is off by %.4f m at %s; this "
                           "rig's rest pose is no longer symmetric enough to "
                           "mirror this way" % (worst, where))
    return {"worst": round(worst, 4), "at": where}


def play(action, slot, frame, mirror=False):
    """Evaluate one action at a possibly fractional frame."""
    arm = _arm()
    ad = arm.animation_data or arm.animation_data_create()
    ad.action = action
    if slot is not None:
        ad.action_slot = slot
    whole = int(math.floor(frame))
    bpy.context.scene.frame_set(whole, subframe=float(frame - whole))
    bpy.context.view_layer.update()
    if mirror:
        mirror_pose()


def hips_world():
    arm = _arm()
    return arm.matrix_world @ arm.pose.bones[HIPS].matrix


def hips_yaw():
    """Heading of the hips about the world vertical, in degrees.

    Read off the bone's own X axis rather than off a forward vector: what the
    number means is not important, only that its DIFFERENCE between two frames
    is the rotation the body turned through.
    """
    side = hips_world().col[0].to_3d()
    return math.degrees(math.atan2(side.y, side.x))


def place_hips(yaw_delta_deg, anchor_xy):
    """Turn the whole body about the vertical and pin it over `anchor_xy`.

    The hips are this rig's root bone, so its world matrix carries the body:
    rotating it about the vertical through its own head turns everything below
    with it, and setting its horizontal translation moves the character without
    touching a single joint angle.
    """
    H = hips_world()
    R = Matrix.Rotation(math.radians(yaw_delta_deg), 4, "Z")
    M = (R @ H.to_3x3().to_4x4())
    M.translation = Vector((anchor_xy[0], anchor_xy[1], H.translation.z))
    guard.set_bone_world(HIPS, M)


def sword_transform(axes, hand_rest, up, wrist_world, blade_dir, flat_dir):
    """The transform putting the blade along `blade_dir` with the wrist there.

    Generalises make_miniboss_guard.guard_transform, which pins the same three
    degrees of freedom for the one case it needs (blade up, flat forward). Built
    as a change of basis for the same reason: Euler angles can roll the blade
    edge-first through its own sweep without any single number looking wrong.
    """
    flat = Vector(axes["flat_axis"])
    side = Vector(up).cross(flat).normalized()
    src = Matrix(((side.x, up.x, flat.x),
                  (side.y, up.y, flat.y),
                  (side.z, up.z, flat.z)))

    t_up = Vector(blade_dir).normalized()
    t_flat = Vector(flat_dir) - t_up * Vector(flat_dir).dot(t_up)
    t_flat.normalize()
    t_side = t_up.cross(t_flat).normalized()
    dst = Matrix(((t_side.x, t_up.x, t_flat.x),
                  (t_side.y, t_up.y, t_flat.y),
                  (t_side.z, t_up.z, t_flat.z)))
    R = (dst @ src.inverted()).to_4x4()
    pivot = hand_rest.translation
    return (Matrix.Translation(Vector(wrist_world)) @ R
            @ Matrix.Translation(-Vector(pivot)))


def build_hold(wrist_world, blade_dir, flat_dir=Vector((0.0, 0.0, 1.0))):
    """Pose the sword arm onto the sword held as asked, and report the reach."""
    guard.reset_pose()
    axes, hand_rest, _B, up = guard.sword_frame()

    M = sword_transform(axes, hand_rest, up, wrist_world, blade_dir, flat_dir)
    Hr = M @ hand_rest
    blade_world = (M.to_3x3() @ Vector(up)).normalized()

    # Aim the clavicle at the hold before solving the arm. Without it the
    # shoulder stays where the rest pose left it, and 0.705 m of arm from a
    # joint 0.28 m off the midline does not reach as far as a swing needs.
    guard.aim_shoulder("Right", Hr.translation)
    r = guard.ik_arm("Right", Hr.translation, POLE["Right"], Hr)
    return {"right": r, "blade": [round(v, 3) for v in blade_world],
            "tip_radius": round(math.hypot(
                (Hr.translation + blade_world * TIP_FROM_WRIST).x,
                (Hr.translation + blade_world * TIP_FROM_WRIST).y), 3)}


def finger_bones():
    return [b.name for b in _arm().data.bones
            if b.name.startswith("mixamorig:%sHand" % GRIP_SIDE)
            and any(k in b.name for k in ("Thumb", "Index", "Middle", "Ring", "Pinky"))]


def capture_grip():
    """The sword hand's finger curl, lifted off `Idle` in local-basis space."""
    arm = _arm()
    src = bpy.data.objects[GRIP_CLIP]
    play(src.animation_data.action, src.animation_data.action_slot, GRIP_FRAME)
    return {b: arm.pose.bones[b].matrix_basis.copy() for b in finger_bones()}


def apply_grip(grip, weight):
    if weight <= 0.0:
        return
    arm = _arm()
    for b, m in grip.items():
        arm.pose.bones[b].matrix_basis = guard.blend(
            arm.pose.bones[b].matrix_basis, m, weight)


def import_source():
    """Bring in the 360 clip on a scratch armature, deleted before the file saves."""
    old = bpy.data.objects.get(SCRATCH)
    if old:
        bpy.data.objects.remove(old, do_unlink=True)
    before = set(o.name for o in bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=SOURCE_FBX)
    new = [bpy.data.objects[n] for n in set(o.name for o in bpy.data.objects) - before]
    arms = [o for o in new if o.type == "ARMATURE"]
    if len(arms) != 1:
        raise SystemExit("%s produced %d armatures" % (SOURCE_FBX, len(arms)))
    for o in new:
        if o.type == "MESH":
            bpy.data.objects.remove(o, do_unlink=True)
    src = arms[0]
    src.name = SCRATCH
    src.animation_data.action.name = SCRATCH
    src.animation_data.action.use_fake_user = False
    return src.animation_data.action, src.animation_data.action_slot


def frames_of(segment):
    """The source frames one segment is sampled at."""
    start, end, count = segment
    if count == 1:
        return [start]
    step = (end - start) / float(count - 1)
    return [start + step * i for i in range(count)]


def source_yaws(action, slot, frames, mirror=MIRROR):
    """Cumulative yaw at each sample, unwrapped, measured from the first."""
    out, prev, total = [], None, 0.0
    for f in frames:
        play(action, slot, f, mirror)
        y = hips_yaw()
        if prev is not None:
            d = y - prev
            while d > 180.0:
                d -= 360.0
            while d < -180.0:
                d += 360.0
            total += d
        prev = y
        out.append(total)
    return out


def capture_pose():
    arm = _arm()
    return {pb.name: pb.matrix_basis.copy() for pb in arm.pose.bones}


def rest_heading(bone):
    """Heading of a bone's own side axis in the REST pose, in degrees."""
    arm = _arm()
    side = (arm.matrix_world @ arm.data.bones[bone].matrix_local).col[0].to_3d()
    return math.degrees(math.atan2(side.y, side.x))


def bone_heading(bone):
    """How far a bone has turned about the vertical since the rest pose."""
    arm = _arm()
    side = (arm.matrix_world @ arm.pose.bones[bone].matrix).col[0].to_3d()
    d = math.degrees(math.atan2(side.y, side.x)) - rest_heading(bone)
    while d > 180.0:
        d -= 360.0
    while d < -180.0:
        d += 360.0
    return d


def solve_hold(axes, hand_rest, up, anchor_xy, heading, wrist_rest, blade_rest,
               tilt_deg=0.0):
    """Put the sword where a hold asks for it, turned to the body's heading.

    Both holds are authored in the REST frame and rotated about the vertical by
    however far the body has turned, so each turns with the character and does
    nothing else. That is what keeps a hold's shape ours rather than the source
    clip's: the height, the reach and the blade's angle are the numbers at the
    top of this file, and the chest's pitch and roll -- which this clip's pivot
    has plenty of -- do not leak into them.

    Yaw only, and that is load-bearing for the wind as much as for the sweep.
    Held against the CHEST instead, as the guard's hold is, the cocked pose rode
    the chest's own rotation -- so mirroring the body to reverse the turn swung
    the sword from behind the shoulder round to the character's side, changing a
    wind-up nobody asked to change.

    The sweep needs it for a second reason: riding the chest carries its pitch
    and roll as well as its turn, and measured, a chest-held sweep put the tip
    through a 1.2 m bob every revolution -- scraping the floor on one side of
    the turn and swinging over a standing player's head on the other.
    """
    R = Matrix.Rotation(math.radians(heading), 4, "Z")
    pivot = Vector((anchor_xy[0], anchor_xy[1], 0.0))
    blade = Vector(blade_rest).normalized()
    if tilt_deg:
        blade.z = math.tan(math.radians(tilt_deg)) * Vector((blade.x, blade.y, 0.0)).length
    blade = (R.to_3x3() @ blade).normalized()
    wrist = pivot + R.to_3x3() @ (Vector(wrist_rest) - pivot)
    wrist.z = wrist_rest.z

    M = sword_transform(axes, hand_rest, up, wrist, blade, Vector((0.0, 0.0, 1.0)))
    Hr = M @ hand_rest
    guard.aim_shoulder("Right", Hr.translation)
    r = guard.ik_arm("Right", Hr.translation, R.to_3x3() @ POLE["Right"], Hr)
    return r["reach"]


def apply_solved(solve_fn, heading, weight):
    """Run a world-space solve over the evaluated pose, blended by `weight`."""
    if weight <= 0.0:
        return None
    arm = _arm()
    was = {b: arm.pose.bones[b].matrix.copy() for b in ARM_BONES}
    used = solve_fn(heading)
    if weight < 1.0:
        solved = {b: arm.pose.bones[b].matrix.copy() for b in ARM_BONES}
        for b in ARM_BONES:
            arm.pose.bones[b].matrix = guard.blend(was[b], solved[b], weight)
            bpy.context.view_layer.update()
    return used


def smoothstep(t):
    t = min(1.0, max(0.0, t))
    return t * t * (3.0 - 2.0 * t)


def build_body(action, slot, segment, anchor_xy, offset0, hold, rest_offset,
               yaw_base=None, yaw_scale=1.0, yaw_settle=0.0, blade_smooth=0.0,
               weights=None, yaw_align=0.0, settle_from=0.0):
    """One phase, as body poses plus what the arms are asked to do over them.

    Headings are absolute, measured from the source clip's own first frame:
    `offset0` is what the segment's first frame already carries, `yaw_base` is
    where the phase is asked to START, and `yaw_scale` multiplies the rotation
    the segment does on its way. Together they are what turns a segment that
    covers 332 deg into a revolution that covers 360 and hands the next repeat a
    heading it can continue from without a snap.

    `yaw_align` is the same constant on every phase and turns the whole clip: see
    FACE_FRAME. `yaw_settle` is an extra rotation eased in across the phase,
    ending on that value. Only the recovery uses it, to unwind the 25 deg the windup's coil put
    in, so the clip finishes facing exactly where it opened -- a clip that ends
    a few degrees off draws the mesh rotated off its capsule and then snaps
    straight the moment the animator moves on to Idle.

    `rest_offset` converts a heading in that reference into one against the REST
    pose, which is the frame the holds are authored in. It is ONE number for the
    whole clip and is measured once, never per phase: taken per phase it comes
    off bone_heading() already wrapped into +-180, and the three revolutions
    then each read as starting from the same heading as the last -- which put a
    360 deg unwind into every cross-fade and whipped the blade backwards through
    228 deg at each seam before it was measured.

    No arm is posed here. Each frame carries the WEIGHT its hold should be
    applied at and, for the sweep, the heading to solve it against; both are
    cross-faded with the poses and consumed afterwards, so the solve runs once
    on the finished body and no seam can pull the blade off its circle.
    """
    frames = frames_of(segment)
    cum = source_yaws(action, slot, frames)
    if yaw_base is None:
        yaw_base = offset0
    last = max(len(frames) - 1, 1)
    if weights is None:
        weights = [1.0] * len(frames)
    # `settle_from` holds the settle off until that fraction of the phase has
    # gone by. The last revolution's follow-through needs it: eased across the
    # whole turn it carries the pass across the front along with it and buys no
    # margin at all, which is exactly what it was added for.
    def settled(i):
        t = i / float(last)
        if settle_from >= 1.0:
            return 0.0
        return yaw_settle * smoothstep((t - settle_from) / (1.0 - settle_from))

    desired = [yaw_base + cum[i] * yaw_scale + settled(i) + yaw_align
               for i in range(len(frames))]

    out = []
    for i, f in enumerate(frames):
        play(action, slot, f, MIRROR)
        place_hips(desired[i] - (offset0 + cum[i]), anchor_xy)
        # The blade is carried between the body's own heading and a constant
        # rate. Pure body means the blade inherits every stall in the source
        # clip's pivot -- measured, its hips turn 22 deg on one output frame and
        # 7 on another -- and a greatsword that stalls twice a revolution reads
        # as a stutter, not a whirl. Pure constant rate detaches the arms from
        # the shoulders driving them. BLADE_SMOOTH is how far between the two
        # this sits, and the difference shows as the arms leading and trailing
        # the chest, which is what arms do.
        even = desired[0] + (desired[last] - desired[0]) * (i / float(last))
        entry = {"pose": capture_pose(), "wind": 0.0, "sweep": 0.0,
                 "heading": desired[i] + blade_smooth * (even - desired[i]) + rest_offset}
        entry[hold] = weights[i]
        out.append(entry)
    return out


def stitch(parts, fades):
    """Join phases end to end, cross-fading poses, weights and headings alike.

    Returns the timeline and the index each part starts at, which is what the
    hitbox measurement needs: the fan is authored against the SPIN, and after
    two cross-fades its first frame is no longer countable from the phase
    lengths.
    """
    timeline, starts = [], []
    for part, fade in zip(parts, fades):
        if timeline and fade:
            n = min(fade, len(part), len(timeline))
            starts.append(len(timeline) - n)
            for i in range(n):
                w = smoothstep((i + 1) / float(n + 1))
                a, b = timeline[-n + i], part[i]
                timeline[-n + i] = {
                    "pose": {k: guard.blend(a["pose"][k], b["pose"][k], w)
                             for k in b["pose"]},
                    "wind": a["wind"] + (b["wind"] - a["wind"]) * w,
                    "sweep": a["sweep"] + (b["sweep"] - a["sweep"]) * w,
                    "heading": a["heading"] + (b["heading"] - a["heading"]) * w,
                }
            timeline.extend(part[n:])
        else:
            starts.append(len(timeline))
            timeline.extend(part)
    return timeline, starts


def pose_arms(timeline, wind_fn, sweep_fn, grip):
    """Lay the two holds over the finished body, frame by frame."""
    arm = _arm()
    snaps, reach = [], 0.0
    for entry in timeline:
        for name, m in entry["pose"].items():
            arm.pose.bones[name].matrix_basis = m
        bpy.context.view_layer.update()
        apply_solved(wind_fn, entry["heading"], entry["wind"])
        used = apply_solved(sweep_fn, entry["heading"], entry["sweep"])
        apply_grip(grip, max(entry["wind"], entry["sweep"]))
        reach = max(reach, used or 0.0)
        snaps.append({pb.name: pb.matrix_basis.decompose() for pb in arm.pose.bones})
    return snaps, reach


def write(snaps):
    frames = list(range(1, len(snaps) + 1))
    act, slot = guard.write_action(CLIP, snaps, frames)
    guard.holder(CLIP, act, slot)
    return act, slot


def measure(action, slot, count):
    """What the blade does, on this character, frame by frame.

    Reported in the RUNTIME's axes, not Blender's: the game's local frame is
    x right, y up, z forward, and this rig faces -Y with +X to its left, so
    (x, y, z)_game = (-x, z, -y)_blender. Validated against `Attack_H`, whose
    hitbox windows in AttackRegistry were measured the same way.
    """
    arm = _arm()
    axes, hand_rest, _B, up = guard.sword_frame()
    tip0 = Vector(axes["centre"]) + Vector(axes["axis"]) * axes["tip_t"]
    guard_pt0 = Vector(axes["centre"]) + Vector(axes["axis"]) * axes["guard_t"]
    grip0 = Vector(axes["centre"]) + Vector(axes["axis"]) * axes["grip_end_t"]

    rows = []
    for f in range(1, count + 1):
        play(action, slot, f)
        H = arm.matrix_world @ arm.pose.bones["mixamorig:RightHand"].matrix
        M = H @ hand_rest.inverted()
        def game(p):
            w = M @ p
            return Vector((-w.x, w.z, -w.y))
        tip, gd, grip = game(tip0), game(guard_pt0), game(grip0)
        toes = min((arm.matrix_world @ arm.pose.bones[b].matrix).translation.z
                   for b in ("mixamorig:LeftToeBase", "mixamorig:RightToeBase"))
        low = min(tip.y, gd.y, grip.y)
        rows.append({
            "f": f, "t": (f - 1) / 30.0,
            "tip": tip, "guard": gd, "grip": grip,
            "angle": math.degrees(math.atan2(tip.x, tip.z)),
            "radius": math.hypot(tip.x, tip.z),
            "below_feet": low - toes,
        })
    return rows


def cum_table(action, slot, lo, hi):
    """Unwrapped heading at every integer source frame, measured from `lo`."""
    frames = list(range(int(lo), int(hi) + 1))
    cum = source_yaws(action, slot, frames)
    return dict(zip(frames, cum))


def unwrapped(rows, spin_start, spin_end):
    """The measured tip angle over the spin, unwrapped so it climbs past 180."""
    live = [r for r in rows if spin_start <= r["f"] - 1 < spin_end]
    ang, prev, total = [], None, 0.0
    for r in live:
        a = r["angle"]
        if prev is not None:
            d = a - prev
            while d > 180.0:
                d -= 360.0
            while d < -180.0:
                d += 360.0
            total += d
        prev = a
        ang.append(total)
    return live, ang


def crossing_time(live, ang, target):
    """When the blade first reaches `target` degrees, in seconds from frame 1."""
    for i in range(1, len(ang)):
        lo, hi = sorted((ang[i - 1], ang[i]))
        if lo <= target <= hi and ang[i] != ang[i - 1]:
            w = (target - ang[i - 1]) / (ang[i] - ang[i - 1])
            return live[i - 1]["t"] + w * (live[i]["t"] - live[i - 1]["t"])
    return None


def front_passes(rows, spin_start, spin_end, half=FRONT_HALF_DEG):
    """When the blade crosses the character's FRONT, once per revolution.

    One hit window per turn, which is what the attack is: three passes of the
    blade, three chances to be hit. The window is the time the tip spends inside
    +-`half` degrees of straight ahead -- read off the clip rather than assumed,
    because the turn is not at a constant rate and the pass is not the same
    length in every revolution.
    """
    live, ang = unwrapped(rows, spin_start, spin_end)
    # Where straight ahead sits on the unwrapped track, and once per revolution
    # either side of it. Enumerated over the track's own range rather than
    # walked forward from a normalised start: the turn runs whichever way the
    # clip turns, and a walk that assumed one direction found no passes at all
    # the first time the spin was mirrored.
    base = -live[0]["angle"]
    lo, hi = min(ang), max(ang)
    # Where the turn runs out. The track is monotone until the recovery starts
    # winding it back, and the last pass finishes inside that reversal: the
    # blade sweeps into the front and stops there rather than carrying on
    # through it, so its window is clamped to the turn instead of being thrown
    # away for want of a far edge that never arrives.
    turning = (max if ang[-1] > ang[0] else min)(range(len(ang)), key=lambda i: ang[i])
    turn_end = live[turning]["t"]

    out = []
    k = int(math.floor((lo - base) / 360.0))
    while base + 360.0 * k <= hi:
        centre = base + 360.0 * k
        edges = [t for t in (crossing_time(live, ang, centre - half),
                             crossing_time(live, ang, centre + half))
                 if t is not None]
        crossed = crossing_time(live, ang, centre)
        if edges and crossed is not None:
            enter = min(edges)
            exit_ = max(edges + [crossed])
            if len(edges) < 2:
                exit_ = max(exit_, turn_end)
            out.append((enter, exit_))
        k += 1
    return sorted(out)


def fan(rows, spin_start, spin_end, sectors=SECTORS):
    """The hitbox fan, as the time the blade spends in each sector of a turn.

    The runtime cannot sweep a hitbox, so the sweep is cut into `sectors` equal
    wedges of the turn and each gets a window of its own, live for exactly as
    long as the blade is inside it. Those windows are NOT equal in length: the
    clip's own turn runs between 9 and 28 deg a frame, and a fan cut evenly in
    TIME would put its widest gaps exactly where the blade moves fastest.

    Cut in angle instead, the three revolutions come out identical -- they are
    the same 20 frames three times -- so one revolution's worth of durations is
    the whole table, and the deviation across the three is reported as proof of
    that rather than assumed.
    """
    live = [r for r in rows if spin_start <= r["f"] - 1 < spin_end]
    # Unwrap the measured tip angle, which turns monotonically once a
    # revolution and is what the sector boundaries are found on.
    ang, prev, total = [], None, 0.0
    for r in live:
        a = r["angle"]
        if prev is not None:
            d = a - prev
            while d > 180.0:
                d -= 360.0
            while d < -180.0:
                d += 360.0
            total += d
        prev = a
        ang.append(total)
    step = math.copysign(360.0 / sectors, ang[-1])
    a0 = ang[0]

    def crossing(target):
        """When the blade reaches `target` degrees, in seconds from the clip's start."""
        for i in range(1, len(ang)):
            lo, hi = sorted((ang[i - 1], ang[i]))
            if lo <= target <= hi and ang[i] != ang[i - 1]:
                w = (target - ang[i - 1]) / (ang[i] - ang[i - 1])
                return live[i - 1]["t"] + w * (live[i]["t"] - live[i - 1]["t"])
        return None

    edges = []
    k = 0
    while True:
        t = crossing(a0 + step * k)
        if t is None:
            break
        edges.append(t)
        k += 1
    count = len(edges) - 1
    if count < sectors:
        raise RuntimeError("the fan found only %d sectors" % count)

    windows = [edges[i + 1] - edges[i] for i in range(count)]
    first = windows[:sectors]
    drift = max(abs(windows[i] - first[i % sectors]) for i in range(len(windows)))
    return {
        "t0": edges[0],
        "t_end": edges[count],
        "sectors": count,
        "turns": round(count / float(sectors), 2),
        "sector_deg": round(step, 2),
        "first_angle": round(live[0]["angle"], 1),
        "windows": [round(w, 4) for w in first],
        "all_windows": [round(w, 4) for w in windows],
        # The same table in RUNTIME frames. Everything downstream of this clip
        # runs at 60 Hz -- raylib resamples every clip to it, tools/
        # bake_root_motion.py bakes at it -- and the state machine can only
        # start and end a hit window on a frame boundary. Quantising here is
        # what stops 22 windows each rounding up a fraction of a frame and the
        # fan finishing two sectors behind the blade it is meant to be.
        "frames60": [int(round(w * 60.0)) for w in windows],
        "frames60_per_turn": sum(int(round(w * 60.0)) for w in first),
        "repeat_drift": round(drift, 4),
        "radius": [round(min(r["radius"] for r in live), 2),
                   round(max(r["radius"] for r in live), 2)],
        "height": [round(min(r["tip"].y for r in live), 2),
                   round(max(r["tip"].y for r in live), 2)],
    }


def main():
    arm = _arm()
    anchor = arm.matrix_world @ arm.data.bones[HIPS].matrix_local
    anchor_xy = (anchor.translation.x, anchor.translation.y)

    grip = capture_grip()
    report = {"wind": build_hold(WIND_WRIST, WIND_BLADE)}
    guard.reset_pose()

    # Both holds are re-solved per frame, so what they need is the sword's own
    # frame rather than a captured pose.
    axes, hand_rest, _B, up = guard.sword_frame()

    def wind(heading):
        return solve_hold(axes, hand_rest, up, anchor_xy, heading,
                          WIND_WRIST, WIND_BLADE)

    def sweep(heading):
        return solve_hold(axes, hand_rest, up, anchor_xy, heading,
                          SWEEP_WRIST, SWEEP_BLADE, SWEEP_TILT_DEG)

    act, slot = import_source()
    head = cum_table(act, slot, 1, int(RECOVER[1]) + 1)

    # The clip's own opening heading, against the rest pose. Every heading below
    # is measured from this one frame, so they stay continuous across three
    # revolutions instead of wrapping.
    report["mirror"] = check_mirror(act, slot, SPIN_SEGMENT[0]) if MIRROR else None
    play(act, slot, WINDUP[0], MIRROR)
    place_hips(0.0, anchor_xy)
    rest_offset = bone_heading(HIPS)
    report["rest_offset"] = round(rest_offset, 2)

    # Turn the whole clip so the hold faces the entity's forward. See FACE_FRAME.
    align = -(head[int(FACE_FRAME)] + rest_offset)
    report["yaw_align"] = round(align, 2)

    # What the spin segment turns on its own, which is what sets the scale each
    # repeat needs to close a full revolution.
    spin_cum = source_yaws(act, slot, frames_of(SPIN_SEGMENT))
    span = spin_cum[-1]
    if abs(span) < 180.0:
        raise RuntimeError("SPIN_SEGMENT turns only %.1f deg" % span)
    revolution = math.copysign(REVOLUTION, span)
    yaw_scale = revolution / span
    report["spin"] = {"segment_span": round(span, 1),
                      "yaw_scale": round(yaw_scale, 4),
                      "frames_per_revolution": SPIN_SEGMENT[2]}

    parts, fades = [], []

    n = WINDUP[2]
    ramp = [min(1.0, (i + 1) / float(n - 2)) for i in range(n)]
    parts.append(build_body(act, slot, WINDUP, anchor_xy, head[int(WINDUP[0])],
                            "wind", rest_offset, weights=ramp, yaw_align=align))
    fades.append(0)

    parts.append(build_body(act, slot, HOLD, anchor_xy, head[int(HOLD[0])],
                            "wind", rest_offset, yaw_align=align))
    fades.append(0)

    # Every repeat starts where the last one ended, one revolution on.
    spin_start = head[int(HOLD[1])]
    for k in range(SPINS):
        parts.append(build_body(act, slot, SPIN_SEGMENT, anchor_xy,
                                head[int(SPIN_SEGMENT[0])], "sweep", rest_offset,
                                yaw_base=spin_start + revolution * k,
                                yaw_scale=yaw_scale, blade_smooth=BLADE_SMOOTH,
                                yaw_settle=(SPIN_OVERSHOOT if k == SPINS - 1
                                            else 0.0),
                                settle_from=OVERSHOOT_FROM,
                                yaw_align=align))
        fades.append(ENTRY_FADE if k == 0 else CROSSFADE)
    spin_end = spin_start + revolution * SPINS + SPIN_OVERSHOOT

    n = RECOVER[2]
    fall = [1.0 if i < HOLD_OUT
            else max(0.0, 1.0 - (i - HOLD_OUT) / float(n - HOLD_OUT - 6))
            for i in range(n)]
    recover_cum = source_yaws(act, slot, frames_of(RECOVER))
    # Land on a whole number of revolutions from where the clip opened.
    settle = revolution * SPINS - (spin_end + recover_cum[-1])
    report["settle_deg"] = round(settle, 2)
    parts.append(build_body(act, slot, RECOVER, anchor_xy, head[int(RECOVER[0])],
                            "sweep", rest_offset, yaw_base=spin_end, yaw_settle=settle,
                            weights=fall, yaw_align=align))
    fades.append(CROSSFADE)

    timeline, starts = stitch(parts, fades)
    snaps, reach = pose_arms(timeline, wind, sweep, grip)
    report["spin"]["worst_arm_reach"] = round(reach, 2)
    new_act, new_slot = write(snaps)
    report["frames"] = len(snaps)
    report["seconds"] = round(len(snaps) / 30.0, 3)

    rows = measure(new_act, new_slot, len(snaps))
    arm.animation_data.action = None
    bpy.data.objects.remove(bpy.data.objects[SCRATCH], do_unlink=True)

    report["parts"] = starts
    # The fan is measured over the spin's CLEAN frames only: the fade in from
    # the hold and the fade out into the recovery are the blade travelling
    # between two different holds, where the turn is not yet the turn.
    # Scanned to the end of the clip, not to the recovery: the third revolution
    # finishes INSIDE the recovery's first frames, which is what HOLD_OUT is for.
    report["fan"] = fan(rows, starts[2] + ENTRY_FADE, len(rows))
    report["front"] = [(round(a, 4), round(b, 4))
                       for a, b in front_passes(rows, starts[2] + ENTRY_FADE,
                                                len(rows))]

    worst = min(rows, key=lambda r: r["below_feet"])
    report["lowest_sword_below_feet"] = round(worst["below_feet"], 3)
    report["lowest_at"] = worst["f"]

    print("MAKE_MINIBOSS_SPIN " + repr(report))
    f = report["fan"]
    print("  the fan, for AttackRegistry: %d sectors of %.0f deg, opening at "
          "%.1f deg, live %.3f-%.3f s" % (f["sectors"], abs(f["sector_deg"]),
                                          f["first_angle"], f["t0"], f["t_end"]))
    print("      seconds: " + ", ".join("%.3f" % w for w in f["all_windows"]))
    print("      frames60: " + ", ".join(str(n) for n in f["frames60"])
          + "   (%d a turn, %d live)" % (f["frames60_per_turn"], sum(f["frames60"])))
    print("  the front passes, for AttackRegistry: +-%.0f deg of straight ahead"
          % FRONT_HALF_DEG)
    prev = 0.0
    for i, (a, b) in enumerate(report["front"]):
        print("      pass %d  %.3f-%.3f s   gap %.3f  live %.3f   "
              "(frames60: gap %d live %d)"
              % (i + 1, a, b, a - prev, b - a,
                 int(round((a - prev) * 60.0)), int(round((b - a) * 60.0))))
        prev = b
    print("  f    t      tip(x,y,z)game        angle  radius  below_feet")
    for r in rows:
        print("  %3d %5.3f  %6.2f %6.2f %6.2f  %7.1f  %5.2f  %6.2f"
              % (r["f"], r["t"], r["tip"].x, r["tip"].y, r["tip"].z,
                 r["angle"], r["radius"], r["below_feet"]))
    return report


if __name__ == "__main__":
    main()
    if "--save" in sys.argv:
        bpy.ops.wm.save_mainfile()
        print("SAVED " + bpy.data.filepath)
