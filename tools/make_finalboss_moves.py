"""Author the final boss's strafes and its three attacks.

    blender -b ~/Documents/3D/Model/pack_finalboss.blend \
        --python tools/make_finalboss_moves.py -- --save

Run LAST of the authoring passes. It consumes the `src_` scratch clips that
tools/build_finalboss_pack.py imported and DELETES them on the way out, because
merge_animations.py exports every armature in the file that carries an action --
a scratch holder left behind is a clip shipped by accident.

The strafes
-----------
Generated from Walk, as asked; the pack ships no strafe of its own.

  StrafeFwd   Walk, unchanged. A forward strafe and a walk are the same motion.
  StrafeBack  Walk played backwards, so the legs step back and the travel
              reverses with them. Cheaper and better than yawing 180, which
              would turn the character away from the player.
  StrafeLeft  Walk rotated 90 deg about the vertical through the origin -- which
  StrafeRight turns the TRAVEL sideways but also turns the character -- with the
              spine then counter-rotated by the same 90 so the chest and head
              come back to facing forward. The legs stride along +-X while the
              torso still faces the player, which is what a strafe is.

That counter-rotation is a real 90 deg of waist twist, and it is the honest cost
of generating a strafe from a walk rather than animating one: a real sidestep
crosses its feet instead of twisting. It reads at gameplay distance on a
hunched, heavy character. If it ever stops reading, the fix is a strafe clip,
not a bigger correction here.

The attacks
-----------
All three are built by cutting one swing out of a source clip and repeating it,
alternating arms by MIRRORING (see finalboss_rig.mirror_basis, verified to
1.1 mm against this rig's own symmetry). Counts are as specified: five
alternating swipes, two after the jump, one after the punch.

Mirroring is what makes alternation possible at all. `mutant swiping.fbx` is a
two-armed combo, not a series of single-arm swings -- measured, both fists move
together through its middle -- so there is no pair of clean left and right
swings inside it to sequence. One swing mirrored gives a genuine other-arm
version of the same motion, on a character whose two arms are nothing alike.

The swings are also RETIMED. Played at its authored rate the swipe takes 22
frames, and five of those end to end is a 5 s attack with no opening for the
player. Resampled at SWING_RATE the flurry lands near 3 s, which is a long
committed window but a punishable one.
"""

import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mathutils import Vector

import finalboss_rig as rig

# --- strafes ---------------------------------------------------------------
# +90 about Z takes the rig's forward (-Y) to its left (+X); see the axis note
# in finalboss_rig. So a POSITIVE yaw is the LEFT strafe.
#
# 55, not 90, and the reason is the transition rather than the pose. At +-90 the
# two clips' pelvises are exactly 180 apart, which is the degenerate case for
# quaternion slerp: the shortest arc between them is undefined, so the 0.15 s
# cross-fade the animator runs when the boss reverses its circling can swing the
# hips either way, and it drags the legs through a half-turn to get there. That
# is what threw the model out of its capsule mid-strafe. At +-55 the two are
# 110 apart, well clear of the degeneracy, and the fade is a little over half
# the arc.
#
# The cost is that the legs now stride 35 off the direction of travel instead of
# along it. That costs nothing real here: an enemy's travel comes from its AI,
# not from its clips, and the renderer cancels clip travel regardless -- so the
# feet were never going to match the ground speed anyway. What the yaw buys is
# only that the stride READS as sideways, and 55 still reads.
STRAFE_YAW = {"StrafeLeft": 55.0, "StrafeRight": -55.0}

# How the counter-rotation is shared out. The neck takes a slice so the head
# finishes square to the front rather than riding the chest's residual twist.
COUNTER_SPINE = [0.30, 0.35, 0.35]
COUNTER_NECK_SHARE = 0.18

# --- attacks ---------------------------------------------------------------
# One swing, cut out of `mutant swiping.fbx` by watching the left fist cross the
# body: it is out at +0.81 on frame 22 and has swept through to -1.27 by 43.
SWING = (19.0, 43.0)
SWING_RATE = 1.45          # source frames consumed per output frame

# The wind-up and the recovery, taken from the same clip's head and tail so the
# flurry starts and ends on the character's own idle stance.
WINDUP = (4.0, 18.0)
RECOVER = (66.0, 80.0)

CROSSFADE = 3              # frames of blend where two segments meet

# Metres a travelling clip may slip backwards between two frames before
# hold_horizontal() calls it a defect. See the note at the check itself.
BACKSTEP_TOLERANCE = 0.02

RAPID_SWINGS = 5           # alternating, starting with the left club
JUMP_SWINGS = 2
PUNCH_SWINGS = 1

SCRATCH_PREFIX = "src_"


def resampled(clip, start, end, rate):
    """The source frames one segment plays, at `rate` source-frames per frame."""
    out, t = [], start
    while t <= end:
        out.append(t)
        t += rate
    return out


def segment(clip, frames, mirror=False, flatten=False):
    """Capture a run of source frames as a list of local-basis poses.

    `flatten` pins the hips' HORIZONTAL position to the segment's first frame,
    in world space, keeping the vertical. It is for a window cut out of the
    middle of a larger motion, which is what every appended swing here is.

    `mutant swiping.fbx` lunges into its sweep and only recovers afterwards, so
    frames 19-43 carry the lunge WITHOUT its return. detrend() removes the net
    displacement but not the excursion, and the excursion is the part a viewer
    sees: measured, the stitched Attack threw the body 0.453 m off its capsule
    where the source punch it was built from moves 0.164 m -- nearly three times
    as far, on a clip that is supposed to be a punch and one swing. The boss
    visibly left its own collision capsule and came back.

    Flattening puts the swing's read where it belongs, in the spine, arms and
    legs. The body stops travelling; the swing does not stop swinging.
    """
    act, slot = rig.source_of(clip)
    poses, anchor = [], None
    for t in frames:
        rig.play(act, slot, t)
        if flatten:
            here = rig.bone_world(rig.HIPS).translation
            if anchor is None:
                anchor = here.copy()
            else:
                rig.offset_bone_world(rig.HIPS, Vector(
                    (anchor.x - here.x, anchor.y - here.y, 0.0)))
        pose = rig.capture_basis()
        poses.append(rig.mirror_basis(pose) if mirror else pose)
    rig.detach()
    return poses


def detrend(part):
    """Remove a segment's NET hips displacement, keeping its motion.

    A swing cut out of the middle of an in-place clip is not itself in place:
    `mutant swiping.fbx` nets zero over all 81 frames, but the frames 19-43 that
    contain the actual sweep lunge forward and only come back afterwards. Joined
    end to end, five of those lunges accumulate -- measured, Attack_Rapid came
    out travelling 3.795 m forward during what is supposed to be a stationary
    flurry.

    Subtracting a linear ramp of the segment's own drift leaves every frame's
    shape intact and puts the last frame back where the first one was. All three
    components go, not just the horizontal pair: the hips' translation is in
    BONE space, whose axes are not the world's, and an in-place segment has
    nothing worth keeping in any of them.
    """
    if len(part) < 2:
        return part
    start = part[0][rig.HIPS].to_translation()
    drift = part[-1][rig.HIPS].to_translation() - start
    last = len(part) - 1
    for i, pose in enumerate(part):
        m = pose[rig.HIPS].copy()
        m.translation = m.translation - drift * (i / float(last))
        pose[rig.HIPS] = m
    return part


def stitch(name, parts):
    """Bake (poses, keep_travel) segments end to end, cross-fading at the joins.

    The fade is between two poses of the SAME skeleton in local-basis space, so
    it swings each joint along an arc rather than sliding it through space --
    the distinction that cost this pack a corpse standing up mid-collapse before
    it was understood (see make_finalboss_break.build_death).

    Segments are RE-ANCHORED at the hips before joining. Every source clip
    starts its hips near the origin, so plain concatenation throws travel away:
    measured, Attack_Jump came out with 0.007 m of net displacement after a leap
    covering more than a metre, because the swipe appended to it snapped the
    hips back to where the swipe began. The boss would have lunged and slid
    back. Anchoring each segment onto the previous one's end keeps the travel
    that segments marked keep_travel are carrying, and detrend() takes it out of
    the ones that should not be carrying any.
    """
    anchored, carry = [], None
    for poses, keep_travel in parts:
        part = [dict(pose) for pose in poses]
        if not keep_travel:
            part = detrend(part)
        if carry is not None:
            shift = carry - part[0][rig.HIPS].to_translation()
            for pose in part:
                m = pose[rig.HIPS].copy()
                m.translation = m.translation + shift
                pose[rig.HIPS] = m
        carry = part[-1][rig.HIPS].to_translation()
        anchored.append(part)

    timeline = []
    for part in anchored:
        if timeline and CROSSFADE:
            n = min(CROSSFADE, len(part), len(timeline))
            for i in range(n):
                w = rig.ease((i + 1) / float(n + 1))
                timeline[-n + i] = {
                    b: rig.blend(timeline[-n + i][b], part[i][b], w)
                    for b in part[i]}
            timeline.extend(part[n:])
        else:
            timeline.extend(part)

    def pose_fn(i):
        rig.reset_pose()
        rig.apply_basis(timeline[i], 1.0)

    return rig.bake(name, len(timeline), pose_fn)


def swing_poses(count):
    """`count` swings, alternating arms, starting with the left club."""
    frames = resampled("src_Swipe", SWING[0], SWING[1], SWING_RATE)
    return [segment("src_Swipe", frames, mirror=bool(i % 2), flatten=True)
            for i in range(count)]


def rest_hips_xy():
    """The hips' horizontal REST position -- where every clip has to sit.

    The runtime draws every clip at the character's capsule and cancels only the
    ROOT bone, which starts each clip at zero by construction. Nothing cancels
    the HIPS. So a clip whose hips start displaced from the rest position is
    drawn displaced, for its whole length, with no mechanism anywhere to notice.
    """
    rig.reset_pose()
    h = rig.bone_world(rig.HIPS).translation
    return Vector((h.x, h.y, 0.0))


def make_in_place(clip):
    """Take a locomotion cycle's net travel out and seat it on the origin.

    Subtracts a linear ramp of the cycle's own horizontal displacement, which is
    what Mixamo's "In Place" export does and what keeps the loop seamless: the
    last frame lands back on the first, so there is no seam to blend across.

    Then seats the result at the rest position, which the ramp alone does NOT
    do. Removing the net only makes the cycle end where it began -- it says
    nothing about WHERE that is. StrafeBack is Walk played backwards, so its
    first frame is Walk's LAST, 1.74 m down the track; de-trended about that
    frame, the whole clip sat 1.74 m forward of every other clip in the pack.
    Root came out at zero, so hasMotion was false, so the renderer cancelled
    nothing and drew the boss a metre and three quarters outside its own capsule
    for the entire clip -- sliding out as the strafe faded in and back as it
    faded out. Measured against the Attack pose: a mean joint gap of 1.642 m,
    against StrafeFwd's 0.337.

    The strafes need this and Walk does not. Nothing consumes a strafe's clip
    travel -- an enemy moves at the speed its AI writes, and SwordmanAnimator
    reads locomotion_speed off the WALK clip alone -- so the 1.73 m each strafe
    was carrying bought nothing, while costing a cancellation swing on every
    direction change. Walk keeps its travel: locomotion_speed is measured from
    it, and the run threshold depends on it.
    """
    act, slot = rig.source_of(clip)
    f0, f1 = (int(v) for v in act.frame_range)
    frames = list(range(f0, f1 + 1))
    anchor = rest_hips_xy()

    track = []
    for f in frames:
        rig.play(act, slot, f)
        track.append(rig.bone_world(rig.HIPS).translation.copy())
    net = track[-1] - track[0]
    seat = Vector((track[0].x, track[0].y, 0.0)) - anchor
    last = max(len(frames) - 1, 1)

    snaps = []
    for i, f in enumerate(frames):
        rig.play(act, slot, f)
        t = i / float(last)
        rig.offset_bone_world(rig.HIPS, Vector(
            (-net.x * t - seat.x, -net.y * t - seat.y, 0.0)))
        snaps.append(rig.snapshot())

    new_act, new_slot = rig.write_action(clip, snaps, frames)
    rig.holder(clip, new_act, new_slot)
    rig.detach()
    return {"removed_m": round(Vector((net.x, net.y, 0.0)).length, 3),
            "reseated_m": round(seat.length, 3)}


def build_strafes():
    report = {}
    walk, slot = rig.source_of("Walk")
    f0, f1 = (int(v) for v in walk.frame_range)
    span = f1 - f0 + 1

    def copy_of(name, order):
        def pose_fn(i):
            rig.play(walk, slot, order[i])
        return rig.bake(name, len(order), pose_fn)

    report["StrafeFwd"] = copy_of("StrafeFwd", list(range(f0, f1 + 1)))
    report["StrafeBack"] = copy_of("StrafeBack", list(range(f1, f0 - 1, -1)))

    for name, yaw in STRAFE_YAW.items():
        def pose_fn(i, yaw=yaw):
            rig.play(walk, slot, f0 + i)
            # About the ORIGIN, not the hips: this turns the clip's travel as
            # well as the body. make_in_place() then takes the travel back out;
            # what is kept is the direction the legs stride in.
            rig.rotate_bone_world(rig.HIPS, yaw, "Z", (0.0, 0.0, 0.0))
            rig.curl_chain(rig.SPINE, -yaw * (1.0 - COUNTER_NECK_SHARE), "Z",
                           weights=COUNTER_SPINE)
            rig.curl_chain(rig.NECK, -yaw * COUNTER_NECK_SHARE, "Z",
                           weights=[0.5, 0.5])
        report[name] = rig.bake(name, span, pose_fn)

    for name in ("StrafeFwd", "StrafeBack", "StrafeLeft", "StrafeRight"):
        report[name]["in_place"] = make_in_place(name)
        report[name]["travel"] = [round(v, 3) for v in rig.hips_travel(name)]
    return report


def hold_horizontal(clip, from_frame):
    """Freeze the hips' HORIZONTAL position from the leap's peak to the end.

    Only for a clip that travels. bake_root_motion.py lifts a clip's travel onto
    the Root bone by projecting the hips' motion onto the clip's net travel
    axis -- and it projects ALL of it, including motion that is body sway rather
    than locomotion. Its own docstring says lateral sway must stay on the hips
    for exactly that reason; what it cannot see is sway that runs ALONG the
    travel axis.

    That is what the swings appended after this leap do. Measured on the
    exported GLB before this existed, Attack_Jump's Root read:

        t=1.90  1.650   (landed)
        t=2.17  1.357
        t=2.43  1.596
        t=2.70  1.269
        t=2.97  1.706   <- +0.44 m in a quarter second

    Non-monotonic, and the runtime believes it: Swordman feeds Root deltas
    straight into the character controller, so the boss lurched most of half a
    metre forward as the attack ended, leaving the hitboxes it had already
    spawned behind it.
    """
    act, slot = rig.source_of(clip)
    f0, f1 = (int(v) for v in act.frame_range)

    # The hold starts at the clip's furthest-forward frame, not at the caller's
    # hint. `from_frame` is where the jump SEGMENT ends, but the body settles
    # for a few frames past that and drifts back 0.16 m doing it -- which the
    # bake reads as the character reversing at 0.9 m/s. Anchoring on the peak
    # instead makes the projected Root non-decreasing by construction.
    track = [None] * (f1 - f0 + 1)
    for f in range(f0, f1 + 1):
        rig.play(act, slot, f)
        track[f - f0] = rig.bone_world(rig.HIPS).translation.copy()
    net = track[-1] - track[0]
    axis = Vector((net.x, net.y, 0.0))
    axis = axis.normalized() if axis.length > 1e-6 else Vector((0.0, -1.0, 0.0))
    progress = [Vector((p.x, p.y, 0.0)).dot(axis) for p in track]

    # Searched only WITHIN the travelling segment. A global argmax picks the
    # last swing's lunge instead of the landing -- measured, it chose t=2.97 and
    # left the whole oscillation in front of it untouched.
    limit = min(max(from_frame, 1), len(progress))
    from_frame = max(range(limit), key=lambda i: progress[i])

    rig.play(act, slot, f0 + from_frame)
    anchor = rig.bone_world(rig.HIPS).translation.copy()

    snaps, worst = [], 0.0
    for f in range(f0, f1 + 1):
        rig.play(act, slot, f)
        if f >= f0 + from_frame:
            here = rig.bone_world(rig.HIPS).translation
            fix = Vector((anchor.x - here.x, anchor.y - here.y, 0.0))
            worst = max(worst, fix.length)
            rig.offset_bone_world(rig.HIPS, fix)
        snaps.append(rig.snapshot())

    new_act, new_slot = rig.write_action(clip, snaps, list(range(f0, f1 + 1)))
    rig.holder(clip, new_act, new_slot)

    # Assert the property the whole function exists for. bake_root_motion.py
    # projects the hips onto this same axis, so a progress curve that never
    # decreases here is a Root that never decreases there -- and Swordman feeds
    # Root deltas in as velocity, so a decrease is the boss stepping backwards.
    after = []
    for f in range(f0, f1 + 1):
        rig.play(new_act, new_slot, f)
        h = rig.bone_world(rig.HIPS).translation
        after.append(Vector((h.x, h.y, 0.0)).dot(axis))
    rig.detach()
    # Not zero. Authored motion jitters by a millimetre or two between frames
    # and always has; the defect this guards against was 0.29-0.44 m per sample,
    # two orders of magnitude larger.
    drops = [(i, round(b - a, 4))
             for i, (a, b) in enumerate(zip(after, after[1:]))
             if b - a < -BACKSTEP_TOLERANCE]
    if drops:
        raise RuntimeError(
            "%s still moves backwards along its own travel axis after the "
            "hold: %s. bake_root_motion will put that on the Root and the boss "
            "will lurch." % (clip, drops[:6]))

    return {"held_from": from_frame, "peak_progress_m": round(max(progress), 3),
            "max_correction_m": round(worst, 3),
            "monotonic": True, "net_after_m": round(after[-1] - after[0], 3)}


def landing_frame(clip, settle=0.006, window=6):
    """Where a jump has come back down and stopped moving vertically."""
    act, slot = rig.source_of(clip)
    f0, f1 = (int(v) for v in act.frame_range)
    heights = []
    for f in range(f0, f1 + 1):
        rig.play(act, slot, f)
        heights.append(rig.bone_world(rig.HIPS).translation.z)
    rig.detach()
    peak = heights.index(max(heights))
    for i in range(peak, len(heights) - window):
        chunk = heights[i:i + window]
        if max(chunk) - min(chunk) < settle:
            return f0 + i + 2
    return f1


def build_attacks():
    report = {}

    windup = segment("src_Swipe", resampled("src_Swipe", *WINDUP, rate=1.0))
    recover = segment("src_Swipe", resampled("src_Swipe", *RECOVER, rate=1.0))

    report["Attack_Rapid"] = stitch(
        "Attack_Rapid",
        [(windup, False)] + [(sw, False) for sw in swing_poses(RAPID_SWINGS)]
        + [(recover, False)])
    report["Attack_Rapid"]["swings"] = RAPID_SWINGS

    # The jump lands and then swings. Its own tail is dropped at the landing --
    # everything after that is the character standing still.
    land = landing_frame("src_Jump")
    jump = segment("src_Jump", list(range(1, land + 1)))
    # The leap is the ONE segment that keeps its travel: it is a gap-closer,
    # and Swordman consumes it through AttackData::usesRootMotion().
    report["Attack_Jump"] = stitch(
        "Attack_Jump",
        [(jump, True)] + [(sw, False) for sw in swing_poses(JUMP_SWINGS)]
        + [(recover, False)])
    report["Attack_Jump"]["swings"] = JUMP_SWINGS
    report["Attack_Jump"]["land_frame"] = land
    # After the stitch, and only on this clip: it is the only one that travels.
    report["Attack_Jump"]["hold"] = hold_horizontal("Attack_Jump", len(jump))
    report["Attack_Jump"]["travel"] = [
        round(v, 3) for v in rig.hips_travel("Attack_Jump")]

    # The punch is RIGHT-handed (measured: its right fist travels 4.51 against
    # the left's 2.66), so "the other hand" is the left club -- which is the
    # swing as authored, unmirrored.
    punch_act, punch_slot = rig.source_of("src_Punch")
    pf1 = int(punch_act.frame_range[1])
    punch = segment("src_Punch", list(range(1, pf1 + 1)))
    report["Attack"] = stitch(
        "Attack",
        [(punch, False)] + [(sw, False) for sw in swing_poses(PUNCH_SWINGS)]
        + [(recover, False)])
    report["Attack"]["swings"] = PUNCH_SWINGS
    return report


def drop_scratch():
    """Delete the src_ holders, so nothing scratch reaches the GLB."""
    gone = []
    for obj in list(bpy.data.objects):
        if obj.type == "ARMATURE" and obj.name.startswith(SCRATCH_PREFIX):
            gone.append(obj.name)
            bpy.data.objects.remove(obj, do_unlink=True)
    for act in list(bpy.data.actions):
        if act.name.startswith(SCRATCH_PREFIX):
            act.use_fake_user = False
            bpy.data.actions.remove(act)
    return sorted(gone)


def seated_on_origin(tolerance=0.12):
    """Every shipped clip must START with its hips on the character's origin.

    The one invariant that had no check and needed one: nothing in the runtime
    cancels hips displacement, so a clip that opens displaced is drawn displaced
    and looks like the model has left its capsule. Travelling clips are measured
    at their FIRST frame, which is the only frame whose position is not the
    clip's own authored travel.
    """
    anchor = rest_hips_xy()
    bad = {}
    for obj in bpy.data.objects:
        if obj.type != "ARMATURE" or obj.name == rig.MAIN_ARM:
            continue
        act, slot = rig.source_of(obj.name)
        rig.play(act, slot, int(act.frame_range[0]))
        h = rig.bone_world(rig.HIPS).translation
        off = (Vector((h.x, h.y, 0.0)) - anchor).length
        if off > tolerance:
            bad[obj.name] = round(off, 3)
    rig.detach()
    if bad:
        raise RuntimeError(
            "these clips open with their hips off the character's origin: %s. "
            "Nothing cancels hips displacement, so each would be drawn that far "
            "outside its own capsule for its whole length." % bad)
    return {"checked": True, "tolerance_m": tolerance}


def main():
    report = build_strafes()
    report.update(build_attacks())
    report["dropped_scratch"] = drop_scratch()
    report["seated"] = seated_on_origin()
    report["clips"] = sorted(o.name for o in bpy.data.objects
                             if o.type == "ARMATURE" and o.name != rig.MAIN_ARM)
    rig.detach()
    print("MAKE_FINALBOSS_MOVES " + repr(report))
    return report


if __name__ == "__main__":
    main()
    if "--save" in sys.argv:
        bpy.ops.wm.save_mainfile()
        print("SAVED " + bpy.data.filepath)
