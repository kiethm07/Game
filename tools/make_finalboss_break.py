"""Author the final boss's PostureBreak and the Death that continues out of it.

    blender -b ~/Documents/3D/Model/pack_finalboss.blend \
        --python tools/make_finalboss_break.py -- --save

Run AFTER tools/build_finalboss_pack.py (for Idle and src_Dying).

PostureBreak: down on the LEFT knee, the LEFT fist driven into the ground, head
bowed. Same-side, as asked -- the planted fist is on the side whose knee is
down, which braces the body over one diagonal instead of squaring it up. The
left is also the Mutant's oversized arm, so it is the fist worth seeing on the
floor.

Death: the same kneel, continued. The body loses the arm that was holding it up
and goes over.

Why Death starts standing anyway
--------------------------------
The clip runs standing -> kneel -> collapse, and KNEEL_AT_FRAME below records
where in it the kneel lands. That number is what
SwordmanAnimator::death_from_bow_start wants: the runtime enters Death part-way
ONLY when the state it is leaving is PostureBreak, and plays the whole clip from
frame 0 otherwise. So one clip covers both -- a deathblow on a broken guard
continues out of the kneel exactly as asked, and a boss that dies some other way
still falls from its feet instead of snapping down onto a knee first.

Where the collapse comes from
-----------------------------
The settled pose at the end is `mutant dying.fbx`'s own last frame, not a
procedural guess: it is a real authored lying silhouette, and blending into it
costs nothing that inventing one would not cost more of. Only its ENDING is
used. Its opening is a backward stagger from standing, which is a different
clip from the one being built here.

Everything is planted, not trusted
----------------------------------
Both clips run through ground(), which skins the mesh on every frame, finds its
lowest vertex and lowers the hips until that vertex sits on the floor. Mixamo
hip translation is absolute metres authored for whatever body the clip came off,
so a pose blended between two sources lands at neither one's height -- this is
the same defect tools/plant_clip_on_floor.py exists for on the miniboss, applied
here at authoring time instead of as a repair afterwards.
"""

import math
import os
import sys

import bpy
from mathutils import Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import finalboss_rig as rig

MESH = "MutantMesh"
FLOOR = 0.0

# Which side goes down. The knee and the planted fist share it.
DOWN = "Left"
UP = "Right"

# --- the kneel, in world metres -------------------------------------------
# Rest reference: hips 0.926, hip joints at (+-0.137, 0.854), shoulders 1.475,
# head 1.629. Thigh 0.359, shin 0.344.
#
# The fold is MODEST, unlike an earlier version of this pose. That one doubled
# the character over to 0.380 hips and a 0.707 head, because it was solving the
# arm as a 0.532 m limb ending at the hand bone. It is not one: the left arm is
# a 0.953 m club reaching 1.217 m from the shoulder (finalboss_rig.LIMB_TIP), so
# the fist gets to the floor from an ordinary kneel and the spine does not have
# to carry it there. The body reads as braced rather than crumpled, which is the
# pose that was asked for.
# The fold is set by REACH, and measured: the club's skin has to get to the
# floor, and its lowest surface sits about 0.20 m above the axis tip the solver
# aims. So the tip must go BELOW the floor for the fist to rest on it, and the
# shoulder has to be low enough that 1.217 m of arm still covers that drop with
# an elbow bend left over. At hips 0.50 it does not -- the solve came out 99%
# extended and was refused. At 0.44 the arm sits near 87%, which is a braced
# post rather than a locked one.
KNEEL_HIPS = Vector((0.030, 0.060, 0.440))
KNEEL_PELVIS_PITCH = 20.0     # positive about +X pitches forward on this rig
KNEEL_SPINE_PITCH = 34.0      # the rest of the fold, spread across the spine
KNEEL_HEAD_BOW = 24.0

# Where the down-side knee rests, and how the shin lies behind it. The knee's
# horizontal distance from the hip joint is not a constant -- it falls out of
# the thigh length and however high the pelvis ended up -- so it is SOLVED in
# build_kneel() rather than written down here and left to rot when the hip
# height changes.
KNEE_FLOOR = 0.100
SHIN_LIFT = 0.005             # the ankle rides a hair above the shin's own line
DOWN_POLE = Vector((0.10, -0.90, -0.42))

# Up-side leg: foot planted flat out in front, knee high.
UP_ANKLE = Vector((-0.265, -0.330, 0.150))
UP_TOE_AT = Vector((-0.265, -0.500, 0.030))
UP_POLE = Vector((-0.20, -0.90, 0.35))

# Where the planted FIST goes -- the club tip, not the hand bone, which on this
# arm drives no vertices at all. Out in front and to its own side, so the body
# is braced over one diagonal rather than propped up dead centre.
FIST_ON_FLOOR = Vector((0.340, -0.440, 0.060))
FIST_POLE = Vector((0.90, 0.10, -0.35))

# The free hand comes to rest on the raised knee, as in the reference photo.
FREE_HAND_POLE = Vector((-0.80, -0.30, -0.35))

# --- clip shapes -----------------------------------------------------------
BREAK_FALL = 14        # frames the body takes to go down
BREAK_FRAMES = 60      # 2.0 s at 30 fps, inside the 3 s deathblow window
BREAK_TREMBLE = 2.2    # degrees of sway in the hold, so the kneel is not frozen

DEATH_FALL = 18        # standing -> kneel
DEATH_HOLD = 6         # the beat on the knee before the body gives
DEATH_CROSSFADE = 7    # frames to join the kneel onto the authored fall
DEATH_SETTLE = 8       # frames of the settled corpse kept after it stops moving

# Filled in by fit_kneel(): the hip height the fall has to be joined at.
_FIT_KNEEL_HIPS = [0.44]


# The three floor contacts, as the solver currently believes them. Seeded from
# the constants above and then FITTED against the skin by fit_kneel() -- see
# there for why they cannot just be written down.
_FIT = {"knee": KNEE_FLOOR, "fist_z": FIST_ON_FLOOR.z, "up_ankle_z": UP_ANKLE.z}

_MEMBERS = {}


def _members(group):
    """Vertex indices weighted mostly to one group. Cached; the mesh is static."""
    if not _MEMBERS:
        obj = bpy.data.objects[MESH]
        index = {g.name: g.index for g in obj.vertex_groups}
        for name, gid in index.items():
            _MEMBERS[name] = [i for i, v in enumerate(obj.data.vertices)
                              if any(g.group == gid and g.weight > 0.5
                                     for g in v.groups)]
    return _MEMBERS.get(group, [])


def group_low(group):
    """Lowest SKINNED vertex of one vertex group, in world metres.

    The joint is not the contact. A bone's position says nothing about where the
    flesh hanging off it ends up, and on this character the two disagree by more
    than a limb's width in both directions at once.
    """
    obj = bpy.data.objects[MESH]
    ev = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    mw = obj.matrix_world
    idx = _members(group)
    if not idx:
        return float("inf")
    return min((mw @ ev.data.vertices[i].co).z for i in idx)


def fit_kneel(rounds=5, tol=0.008):
    """Slide each floor contact until its own SKIN rests on the floor.

    Three separate contacts -- the down knee and shin, the planted club, and the
    up-side foot -- and they cannot be solved by one number. Measured on the
    first build of this pose: with all three joints placed by hand, the club was
    floating 0.205 m clear while the down toe was 0.177 m under. Grounding the
    whole body afterwards, which is what plant_clip_on_floor.py does, only makes
    that worse -- it lifts everything by the deepest offender, so correcting the
    buried toe would have left the knee hanging in the air.

    So each contact is corrected against its own residual, and the pose is
    re-solved between rounds because moving any one of them moves the others.
    Newton with a unit derivative: the correction is a translation of the
    target, so one step is very nearly exact and the loop is really just
    catching the cross-talk.
    """
    groups = {
        "knee": ["mixamorig:%sLeg" % DOWN, "mixamorig:%sFoot" % DOWN,
                 "mixamorig:%sToeBase" % DOWN],
        "fist_z": ["mixamorig:%sForeArm" % DOWN],
        "up_ankle_z": ["mixamorig:%sFoot" % UP, "mixamorig:%sToeBase" % UP],
    }
    history = []
    for _ in range(rounds):
        build_kneel()
        residual = {k: min(group_low(g) for g in gs) for k, gs in groups.items()}
        history.append({k: round(v, 4) for k, v in residual.items()})
        for key, value in residual.items():
            _FIT[key] -= value
        if max(abs(v) for v in residual.values()) < tol:
            break
    report = build_kneel()
    report["fit"] = {k: round(v, 4) for k, v in _FIT.items()}
    report["fit_residuals"] = history
    _FIT_KNEEL_HIPS[0] = rig.bone_world(rig.HIPS).translation.z
    report["skin_low"] = round(min(group_low(g)
                                   for gs in groups.values() for g in gs), 4)
    return report


def build_kneel():
    """Pose the whole body into the broken-posture kneel.

    Torso first, then the legs, then the arms: the leg and arm solves read the
    hip and shoulder joints off wherever the torso left them, so folding the
    spine afterwards would move every target out from under its own solution.
    """
    rig.reset_pose()

    # Pelvis: dropped, shifted onto the down-side knee, and pitched forward.
    hips_rest = rig.bone_world(rig.HIPS).translation.copy()
    rig.offset_bone_world(rig.HIPS, KNEEL_HIPS - hips_rest)
    rig.rotate_bone_world(rig.HIPS, KNEEL_PELVIS_PITCH, "X",
                          rig.bone_world(rig.HIPS).translation)
    rig.curl_chain(rig.SPINE, KNEEL_SPINE_PITCH, "X", weights=[0.3, 0.35, 0.35])
    rig.curl_chain(rig.NECK, KNEEL_HEAD_BOW, "X", weights=[0.45, 0.55])

    # Down-side leg. The knee has to end up ON the floor, so its position is
    # derived from wherever the pelvis actually put the hip joint: forward of it
    # by whatever the thigh has left after the vertical drop. Writing the ankle
    # target as a constant instead would silently stop touching the floor the
    # first time KNEEL_HIPS changed.
    hip = rig.bone_world("mixamorig:%sUpLeg" % DOWN).translation.copy()
    thigh = rig.arm().data.bones["mixamorig:%sUpLeg" % DOWN].length * 0.01
    shin = rig.arm().data.bones["mixamorig:%sLeg" % DOWN].length * 0.01
    drop = hip.z - _FIT["knee"]
    if drop >= thigh:
        raise RuntimeError(
            "the hip joint is %.3f above a knee resting at %.3f, but the thigh "
            "is only %.3f -- the pelvis is too high for this knee to reach the "
            "floor. Lower KNEEL_HIPS." % (hip.z, _FIT["knee"], thigh))
    forward = math.sqrt(thigh * thigh - drop * drop)
    knee = Vector((hip.x, hip.y - forward, _FIT["knee"]))
    ankle = knee + Vector((0.0, shin, SHIN_LIFT))
    toe_at = ankle + Vector((0.0, 0.160, -0.020))
    down_leg = rig.ik_leg(DOWN, ankle, DOWN_POLE, toe_at - ankle)

    up_ankle = Vector((UP_ANKLE.x, UP_ANKLE.y, _FIT["up_ankle_z"]))
    up_leg = rig.ik_leg(UP, up_ankle, UP_POLE, UP_TOE_AT - up_ankle)

    # The planted club. ik_arm targets the visible fist, so this is the point
    # that actually lands on the ground.
    fist_target = Vector((FIST_ON_FLOOR.x, FIST_ON_FLOOR.y, _FIT["fist_z"]))
    plant = rig.ik_arm(DOWN, fist_target, FIST_POLE)

    # The free arm comes down onto the raised knee, as in the reference photo.
    up_knee = rig.bone_world("mixamorig:%sLeg" % UP).translation.copy()
    rest_on = up_knee + Vector((-0.03, -0.04, 0.090))
    free = rig.ik_arm(UP, rest_on, FREE_HAND_POLE,
                      Vector((-0.15, -0.80, -0.45)), target="wrist")
    rig.close_fist(UP)

    down_knee = rig.bone_world("mixamorig:%sLeg" % DOWN).translation
    fist = rig.fist_world(DOWN)
    if down_knee.z > 0.35:
        raise RuntimeError("the down knee settled at %.3f, nowhere near the "
                           "floor -- check DOWN_POLE" % down_knee.z)
    return {
        "down_leg": down_leg, "up_leg": up_leg,
        "planted_arm": plant, "free_arm": free,
        "knee_on_floor_z": round(down_knee.z, 3),
        "fist_on_floor": [round(v, 3) for v in fist],
        "hips_z": round(rig.bone_world(rig.HIPS).translation.z, 3),
        "head_z": round(rig.bone_world("mixamorig:Head").translation.z, 3),
    }


def lowest_vertex():
    """The lowest skinned vertex of the posed body, in world metres."""
    dg = bpy.context.evaluated_depsgraph_get()
    obj = bpy.data.objects[MESH]
    ev = obj.evaluated_get(dg)
    mw = obj.matrix_world
    return min((mw @ v.co).z for v in ev.data.vertices)


# What the body actually rests ON. Deliberately NOT every group: the left club
# is 0.95 m long and swings through a wide arc during the collapse, so if it is
# allowed into the floor test it becomes the lowest vertex on most frames and
# drags the whole body up and down with it. Measured with the club included,
# Death heaved from 0.440 up to 0.795 in mid-fall -- the corpse rearing up --
# purely because grounding was chasing a swinging arm. The arms are checked
# separately by clearance(), which reports rather than corrects.
SUPPORT = ["mixamorig:Hips", "mixamorig:Spine", "mixamorig:Spine1",
           "mixamorig:Spine2", "mixamorig:Head",
           "mixamorig:LeftUpLeg", "mixamorig:LeftLeg", "mixamorig:LeftFoot",
           "mixamorig:LeftToeBase",
           "mixamorig:RightUpLeg", "mixamorig:RightLeg", "mixamorig:RightFoot",
           "mixamorig:RightToeBase"]

ARMS = ["mixamorig:LeftForeArm", "mixamorig:RightForeArm", "mixamorig:RightHand"]


def support_low():
    return min(group_low(g) for g in SUPPORT)


def ground(clip, groups=None):
    """Lower (or lift) each frame's hips until the body rests on the floor.

    Translating the hips moves the whole skeleton rigidly, so the correction is
    exact in one pass and needs no solve. Measured per frame, so it follows the
    collapse instead of imposing one constant offset that would sink the body
    while it is still upright.

    Only SUPPORT groups are consulted -- see the note there.
    """
    act, slot = rig.source_of(clip)
    f0, f1 = (int(v) for v in act.frame_range)
    frames = list(range(f0, f1 + 1))
    groups = groups or SUPPORT

    fixes = []
    snaps = []
    for f in frames:
        rig.play(act, slot, f)
        gap = min(group_low(g) for g in groups)
        rig.offset_bone_world(rig.HIPS, Vector((0.0, 0.0, -gap)))
        fixes.append(gap)
        snaps.append(rig.snapshot())

    new_act, new_slot = rig.write_action(clip, snaps, frames)
    rig.holder(clip, new_act, new_slot)
    rig.detach()
    return {"lowered_max": round(max(fixes), 4),
            "raised_max": round(-min(fixes), 4)}


def clearance(clip):
    """How far the arms dip below the floor across a clip. Reported, not fixed.

    A club passing a few centimetres through the ground for two frames of a fall
    is invisible; correcting it by lifting the body is not.
    """
    act, slot = rig.source_of(clip)
    f0, f1 = (int(v) for v in act.frame_range)
    worst = float("inf")
    at = f0
    for f in range(f0, f1 + 1):
        rig.play(act, slot, f)
        low = min(group_low(g) for g in ARMS)
        if low < worst:
            worst, at = low, f
    rig.detach()
    return {"arm_low": round(worst, 4), "at_frame": at}


def build_posture_break(kneel):
    """Idle on its feet, folding down onto the knee, then held there."""
    base, slot = rig.source_of("Idle")

    def pose_fn(i):
        if i < BREAK_FALL:
            # Blend out of the standing idle rather than snapping: the animator
            # cross-fades into this state over 0.08 s, which is not enough to
            # cover a whole body dropping half a metre.
            rig.play(base, slot, 1 + i)
            standing = rig.capture_basis()
            build_kneel()
            down = rig.capture_basis()
            rig.apply_basis(standing, 1.0)
            rig.apply_basis(down, rig.ease((i + 1) / float(BREAK_FALL)))
        else:
            build_kneel()
            # A held pose with every channel constant reads as a freeze-frame.
            t = (i - BREAK_FALL) / float(max(BREAK_FRAMES - BREAK_FALL - 1, 1))
            sway = BREAK_TREMBLE * math.sin(t * math.pi * 3.0) * (1.0 - t * 0.5)
            rig.curl_chain(rig.SPINE, sway, "X")

    return rig.bake("PostureBreak", BREAK_FRAMES, pose_fn)


def death_segment():
    """Where src_Dying stops moving, so the static tail can be trimmed.

    Its last ~57 frames hold one settled pose. Shipping them would spend half
    the clip on a corpse that has already finished falling, and every frame is
    resampled to 60 Hz by raylib's loader on the way in.
    """
    act, slot = rig.source_of("src_Dying")
    f0, f1 = (int(v) for v in act.frame_range)
    kneel_z = _FIT_KNEEL_HIPS[0]

    heights = []
    for f in range(f0, f1 + 1):
        rig.play(act, slot, f)
        heights.append(rig.bone_world(rig.HIPS).translation.z)
    rig.detach()

    settled = next((i for i, h in enumerate(heights) if h <= kneel_z), 0)
    end = f1
    for i in range(settled + 1, len(heights)):
        window = heights[i:i + 8]
        if len(window) == 8 and max(window) - min(window) < 0.004:
            end = f0 + i + DEATH_SETTLE
            break
    return min(end, f1)


def build_death(end):
    """Death is `mutant dying.fbx`, trimmed -- not a construction.

    Three constructions were tried and measured before settling on this. All of
    them tried to author the standing -> kneel -> collapse in one clip so the
    kneel was literally inside it:

      * blend the kneel into the dying clip's settled last frame: the body ROSE
        from 0.440 to 0.774 mid-collapse, because interpolating to a lying pose
        takes the legs through their straightened middle. It stood up to die.
      * blend into an authored prone pose instead: same failure, same cause.
      * rigidly topple the kneel about its floor contact: descends correctly,
        but ends 0.43 m through the floor, which grounding undoes.
      * cross-fade the kneel onto the authored fall at its closest frame: drove
        0.386 m of body through the floor during the fade, and grounding turned
        that into a 0.26 m hop.

    The runtime already solves this and does not need the clip to. Death plays
    from frame 0 for an ordinary death, and SwordmanAnimator enters it PART-WAY
    -- at death_from_bow_start -- only when the state it is leaving is
    PostureBreak. So the kneel does not have to be inside the clip; the clip
    only has to pass near it, and bow_entry() finds where. This is what the
    miniboss does, and what the mechanism was built for.
    """
    act, slot = rig.source_of("src_Dying")
    f0 = int(act.frame_range[0])

    def pose_fn(i):
        rig.play(act, slot, f0 + i)

    report = rig.bake("Death", end - f0 + 1, pose_fn)
    report["source"] = "src_Dying frames %d-%d" % (f0, end)
    return report


def bow_entry():
    """Where to enter Death when the body is already kneeling, in seconds.

    Measured the way the miniboss's was: score every frame of the fall by the
    mean distance from its joints to the kneel's, and take the best. The number
    the caller wants is not the score itself but the IMPROVEMENT over entering
    at frame 0 -- if the clip never passes near the kneel, moving the entry
    point buys nothing and the runtime should keep the clip's own start.
    """
    act, slot = rig.source_of("Death")
    f0, f1 = (int(v) for v in act.frame_range)

    build_kneel()
    kneel = {b: rig.bone_world(b).translation.copy() for b in SUPPORT + ARMS}
    kneel_z = _FIT_KNEEL_HIPS[0]

    scores, eligible = {}, {}
    for f in range(f0, f1 + 1):
        rig.play(act, slot, f)
        gap = sum((rig.bone_world(b).translation - kneel[b]).length
                  for b in kneel) / len(kneel)
        scores[f] = gap
        # The entry frame may not be HIGHER than the kneel. Scoring on joint
        # distance alone picks frame 32, which is 0.064 m closer to the kneel
        # on average but has the hips at 0.71 against the kneel's 0.44 -- so
        # the body would jump 0.27 m upward on the first frame of its death,
        # which is the exact pop death_from_bow_start exists to remove.
        if rig.bone_world(rig.HIPS).translation.z <= kneel_z + 0.05:
            eligible[f] = gap
    rig.detach()

    if not eligible:
        return {"frame": f0, "seconds": 0.0, "gap_m": round(scores[f0], 3),
                "note": "the fall never reaches the kneel's height; the runtime "
                        "should keep the clip's own start"}

    best = min(eligible, key=eligible.get)
    rig.play(act, slot, f0)
    pop0 = rig.bone_world(rig.HIPS).translation.z - kneel_z
    rig.play(act, slot, best)
    pop = rig.bone_world(rig.HIPS).translation.z - kneel_z
    rig.detach()
    return {
        "frame": best,
        "seconds": round((best - f0) / rig.scene_fps(), 3),
        # Both metrics, because they disagree and the disagreement is the whole
        # decision. Mean joint gap PREFERS frame 0 (0.356 against 0.404): the
        # kneel's limbs happen to be arranged more like a standing stagger than
        # like a mid-fall. But frame 0 is 0.37 m higher than the kneel, and a
        # body jumping a third of a metre upward is what an eye catches --
        # whereas a limb arranged differently at the same height is covered by
        # the animator's own 0.10 s cross-fade into Death.
        "mean_gap_m": round(scores[best], 3),
        "mean_gap_at_frame0_m": round(scores[f0], 3),
        "hip_pop_m": round(pop, 3),
        "hip_pop_at_frame0_m": round(pop0, 3),
    }


def monotonic_drop(clip, from_frame, allow=0.03):
    """Assert the body never gets back UP once the fall has started.

    Not literally monotonic: `mutant dying.fbx` arches and settles after the
    body lands, and its hips genuinely rise about 0.10 doing it. That is a death
    throe, not a defect. What must never happen is the corpse recovering height
    it has already lost -- so the test is against the height the fall STARTED
    at, with `allow` covering the landing bounce.
    """
    act, slot = rig.source_of(clip)
    f0, f1 = (int(v) for v in act.frame_range)
    heights = []
    for f in range(f0 + from_frame, f1 + 1):
        rig.play(act, slot, f)
        heights.append(rig.bone_world(rig.HIPS).translation.z)
    rig.detach()
    climb = max(heights) - heights[0]
    if climb > allow:
        peak = f0 + from_frame + heights.index(max(heights))
        raise RuntimeError(
            "%s climbs %.3f m above where its fall started, peaking at frame "
            "%d. The body is standing back up in the middle of its own "
            "collapse." % (clip, climb, peak))
    return {"drop_m": round(heights[0] - heights[-1], 3),
            "peak_climb_m": round(climb, 4), "from_frame": from_frame}


def main():
    kneel = fit_kneel()
    report = {"kneel": kneel}
    report["PostureBreak"] = build_posture_break(kneel)
    report["PostureBreak"]["ground"] = ground("PostureBreak")
    report["PostureBreak"]["clearance"] = clearance("PostureBreak")

    report["Death"] = build_death(death_segment())
    report["Death"]["ground"] = ground("Death")
    # After grounding, not before: grounding moves the hips too.
    report["Death"]["clearance"] = clearance("Death")
    report["Death"]["descent"] = monotonic_drop("Death", 0, allow=0.12)

    # What SwordmanAnimator::death_from_bow_start has to be set to. The kneel is
    # reached at the end of the fall phase and held through DEATH_HOLD; entering
    # on the first held frame is the closest whole-skeleton match to the pose
    # PostureBreak leaves the body in.
    report["death_from_bow_start"] = bow_entry()

    rig.detach()
    print("MAKE_FINALBOSS_BREAK " + repr(report))
    return report


if __name__ == "__main__":
    main()
    if "--save" in sys.argv:
        bpy.ops.wm.save_mainfile()
        print("SAVED " + bpy.data.filepath)
