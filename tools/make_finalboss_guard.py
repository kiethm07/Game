"""Author the final boss's crossed-arm guard: HitReact, Guard, GuardWalk, GuardImpact.

    blender -b ~/Documents/3D/Model/pack_finalboss.blend \
        --python tools/make_finalboss_guard.py -- --save

Run AFTER tools/build_finalboss_pack.py, which is what puts the Idle and Walk
these are layered over into the file.

The pose is the reference photo: both forearms brought up and crossed in an X in
front of the face, fists closed, head tucked in behind them. Nothing in the pack
resembles it -- the Mutant's clips are all open-armed -- so all four clips here
are solved rather than borrowed.

HitReact is authored here too, and first, because GuardImpact is layered over it
exactly as the miniboss's is. It is the flinch for a hit that did NOT land on a
guard: the blow drives the torso back and the head with it, and the body
recovers over the rest of the clip.

What the two arms force
-----------------------
These arms are not a pair. The left is a single rigid club 0.953 m long with no
hand bone of its own, reaching 1.217 m from the shoulder; the right is an
ordinary arm reaching 0.746 m. Measured off the vertex weights, not assumed --
finalboss_rig.LIMB_TIP has the numbers and what they cost before they existed.

So the cross is deliberately asymmetric: the left club is the long diagonal,
laid from a low left elbow up across the face, and the shorter right forearm
crosses beneath it. Building it the other way round -- or solving either arm to
put its WRIST somewhere -- throws the left fist about a metre past wherever it
was aimed, which is exactly the bug this pose was rebuilt to fix.

Solved, not keyed
-----------------
Same method as tools/make_miniboss_guard.py: choose where the wrists go in world
space and run an analytic two-bone IK up each arm to reach them. Hand-keying a
shoulder and an elbow until a cross looks right from the viewport camera gives a
cross that is right from one camera angle.

Chest-relative, which is what makes GuardWalk work
--------------------------------------------------
The eight arm bones and the two neck bones are captured against
`mixamorig:Spine2` and re-applied against whatever the chest is doing on each
frame of the base clip. Stored in absolute armature space instead, the arms
would hang in place while the legs walked out from under them. Riding the chest
also buys the hold the base clip's breathing for free, so it is not glassy.

Only those ten bones are overwritten. Guard keeps Idle's stance, GuardWalk keeps
Walk's stride and -- asserted below -- its exact net travel, because the runtime
reads a walk's displacement from the clip's own root motion.
"""

import os
import sys

import bpy
from mathutils import Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import finalboss_rig as rig

# The ten bones the guard owns. Parent-first: setting a parent's matrix moves
# its children, so shoulder -> hand and neck -> head.
GUARD_SET = rig.ARM_BONES + rig.NECK

# Where each FIST goes, in world metres -- the visible mass, not the wrist bone.
# The two arms are nothing alike on this character (see finalboss_rig.LIMB_TIP):
# the left is a 0.95 m club with no hand bone of its own and 1.217 m of reach
# from the shoulder, the right is an ordinary arm reaching 0.746 m. So the cross
# is not symmetric and cannot be -- the left club is the long diagonal and the
# right forearm crosses under it.
#
# Rest reference for this character: shoulder joints at (+-0.274, 0.010, 1.475),
# head 1.629, head top 2.038.
# Rendered and adjusted: at 1.880 / 1.620 the club swept up over the crown and
# the head vanished behind the cross entirely. The block has to read as a guard,
# which means the head stays visible above it -- so both come down about 0.22
# and the cross now sits across the face rather than on top of the skull.
LEFT_FIST = Vector((-0.350, -0.560, 1.650))
RIGHT_FIST = Vector((0.270, -0.430, 1.470))

# Which way the right fist points. Its knuckles are carried the last 0.214 m
# past the wrist by the hand bone, so ik_arm needs the direction to place them.
RIGHT_FIST_DIR = Vector((0.180, -0.050, 0.090))

# Elbows driven down and outward -- the pole direction for each arm's solve. A
# guard with the elbows winged out sideways reads as a shrug, not a block.
LEFT_POLE = Vector((0.85, 0.20, -0.50))
RIGHT_POLE = Vector((-0.80, 0.25, -0.55))

# The shoulders come forward and up into the block. Aim targets, not rotations.
LEFT_SHOULDER_AT = Vector((0.220, -0.220, 1.550))
RIGHT_SHOULDER_AT = Vector((-0.220, -0.200, 1.550))

# Head tucked down and in behind the arms, and the chest hunched over the guard.
GUARD_HEAD_BOW = 11.0
GUARD_HUNCH = 8.0

# Frames over which the guard comes up in Guard, with Idle running underneath.
GUARD_RAISE = 6

# Clip lengths. Guard and GuardImpact are cut to their own length; GuardWalk
# takes all of Walk, because a partial stride would not loop.
GUARD_FRAMES = 90
HITREACT_FRAMES = 24

# HitReact. The blow lands on the third frame and the body is still recovering
# when the clip ends -- SwordmanAnimator takes the flinch's duration from the
# clip, so the length IS the timing of the stagger.
HIT_PEAK = 3
HIT_LEAN = -26.0          # negative about +X leans the torso BACK
HIT_HEAD = -16.0
HIT_SHOVE = Vector((0.0, 0.115, -0.035))   # +Y is backwards on this rig

# GuardImpact. The blow lands on a raised cross, so it drives the whole hold
# back into the body and down; the arms then push it out again.
RECOIL_OFFSET = Vector((0.0, 0.105, -0.075))
RECOIL_PEAK = 3
RECOIL_SETTLE = 18

# clip -> base clip it is laid over
SOURCES = {"Guard": "Idle", "GuardWalk": "Walk", "GuardImpact": "HitReact"}


def build_guard(offset=Vector((0.0, 0.0, 0.0))):
    """Pose both arms into the cross, with the whole hold shifted by `offset`.

    Returns what the solve actually achieved -- how much elbow slack each arm
    kept, and where the forearms ended up crossing -- because those are the two
    numbers that say whether the pose is a block or a straight-armed reach.
    """
    rig.reset_pose()

    rig.curl_chain(rig.SPINE, GUARD_HUNCH, "X")
    rig.aim_bone("mixamorig:LeftShoulder", LEFT_SHOULDER_AT + offset)
    rig.aim_bone("mixamorig:RightShoulder", RIGHT_SHOULDER_AT + offset)

    # Left first: it is the long arm, and the right has to cross under wherever
    # the club ends up rather than the other way round.
    left = rig.ik_arm("Left", LEFT_FIST + offset, LEFT_POLE)
    right = rig.ik_arm("Right", RIGHT_FIST + offset, RIGHT_POLE,
                       RIGHT_FIST_DIR)
    fingers = rig.close_fist("Right")

    # Head last: it hangs off the chest, which the hunch already moved.
    rig.curl_chain(rig.NECK, GUARD_HEAD_BOW, "X", weights=[0.4, 0.6])

    l_elbow = rig.bone_world("mixamorig:LeftForeArm").translation.copy()
    r_elbow = rig.bone_world("mixamorig:RightForeArm").translation.copy()
    l_fist = rig.fist_world("Left")
    r_fist = rig.fist_world("Right")
    gap = segment_gap(l_elbow, l_fist, r_elbow, r_fist)
    return {
        "left": left, "right": right, "fingers_curled": fingers,
        "left_elbow": [round(v, 3) for v in l_elbow],
        "right_elbow": [round(v, 3) for v in r_elbow],
        # Measured off the solved skeleton and the real fist positions, not
        # echoed back from the targets. Each limb has to run from its own side
        # of the midline to the other for this to be a cross rather than two
        # arms held up side by side, so both spans must straddle zero.
        "left_limb_x": [round(l_elbow.x, 3), round(l_fist.x, 3)],
        "right_limb_x": [round(r_elbow.x, 3), round(r_fist.x, 3)],
        # And they must not occupy the same space where they cross. The left
        # club alone has a 0.376 m radius, so this gap is measured between the
        # limb AXES and only says the bones are not co-located; the skin check
        # is the bbox in the report.
        "limb_gap": round(gap, 3),
        "left_fist": [round(v, 3) for v in l_fist],
        "right_fist": [round(v, 3) for v in r_fist],
    }


def segment_gap(a0, a1, b0, b1):
    """Closest distance between two line segments, sampled.

    Sampled rather than solved: 24 points a side is well inside a millimetre on
    segments this short, and the closed form has four degenerate cases that all
    have to be right for a number nothing else depends on.
    """
    best = float("inf")
    steps = 24
    for i in range(steps + 1):
        p = a0.lerp(a1, i / float(steps))
        for j in range(steps + 1):
            q = b0.lerp(b1, j / float(steps))
            best = min(best, (p - q).length)
    return best


def build_hitreact():
    """Lay a staggered recoil over Idle and bake it as HitReact.

    Whole-body, unlike the guard: a flinch that only moved the arms would read
    as the character shrugging off the hit. The hips take a shove backwards, the
    spine leans away from the blow and the head goes with it, all on one impulse
    curve so the three cannot drift out of step.
    """
    base, slot = rig.source_of("Idle")

    def impulse(i):
        if i <= HIT_PEAK:
            return rig.ease(i / float(HIT_PEAK))
        tail = HITREACT_FRAMES - 1 - HIT_PEAK
        return 1.0 - rig.ease((i - HIT_PEAK) / float(max(tail, 1)))

    def pose_fn(i):
        rig.play(base, slot, 1 + i)
        w = impulse(i)
        if w <= 1e-4:
            return
        rig.offset_bone_world(rig.HIPS, HIT_SHOVE * w)
        rig.curl_chain(rig.SPINE, HIT_LEAN * w, "X")
        rig.curl_chain(rig.NECK, HIT_HEAD * w, "X")

    return rig.bake("HitReact", HITREACT_FRAMES, pose_fn)


def layered(clip, rel_hold, rel_recoil):
    """Bake one guard clip: `clip`'s base, with the cross laid over its arms."""
    base, slot = rig.source_of(SOURCES[clip])
    f0, f1 = (int(v) for v in base.frame_range)
    span = f1 - f0 + 1
    count = {"Guard": min(GUARD_FRAMES, span), "GuardWalk": span}.get(clip, span)

    def pose_fn(i):
        rig.play(base, slot, f0 + i)
        if clip == "Guard":
            rig.apply_relative(rel_hold, min(1.0, rig.ease(i / float(GUARD_RAISE))))
        elif clip == "GuardWalk":
            rig.apply_relative(rel_hold, 1.0)
        else:
            if i <= RECOIL_PEAK:
                t = rig.ease(i / float(max(RECOIL_PEAK, 1)))
                pose = {k: rig.blend(rel_hold[k], rel_recoil[k], t) for k in rel_hold}
            else:
                t = rig.ease((i - RECOIL_PEAK)
                             / float(max(RECOIL_SETTLE - RECOIL_PEAK, 1)))
                pose = {k: rig.blend(rel_recoil[k], rel_hold[k], t) for k in rel_hold}
            rig.apply_relative(pose, 1.0)

    return rig.bake(clip, count, pose_fn)


def main():
    report = {"HitReact": build_hitreact()}

    report["pose"] = build_guard()
    rel_hold = rig.capture_relative(GUARD_SET)

    build_guard(RECOIL_OFFSET)
    rel_recoil = rig.capture_relative(GUARD_SET)

    for clip in ("Guard", "GuardWalk", "GuardImpact"):
        report[clip] = layered(clip, rel_hold, rel_recoil)
        report[clip]["base"] = SOURCES[clip]

    # The runtime reads a guard-walk's displacement from its root motion, so it
    # has to travel exactly what Walk travels. Only the arms and neck were
    # touched, so this is an assertion that nothing leaked into the hips -- not
    # a correction.
    drift = (rig.hips_travel("GuardWalk") - rig.hips_travel("Walk")).length
    if drift > 1e-4:
        raise RuntimeError("GuardWalk travel drifted %.6f m from Walk" % drift)
    report["GuardWalk"]["travel_drift"] = round(drift, 8)

    rig.detach()
    print("MAKE_FINALBOSS_GUARD " + repr(report))
    return report


if __name__ == "__main__":
    main()
    if "--save" in sys.argv:
        bpy.ops.wm.save_mainfile()
        print("SAVED " + bpy.data.filepath)
