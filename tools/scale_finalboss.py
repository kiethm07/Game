"""Scale the final boss to its final size, as the last step before export.

    blender -b ~/Documents/3D/Model/pack_finalboss.blend \
        --python tools/scale_finalboss.py

Reads pack_finalboss.blend and writes pack_finalboss_scaled.blend. The authoring
file is never modified, which is the point of the split -- see below.

Why the scale lives HERE and not in the runtime
-----------------------------------------------
Enemy::visual_size scales the DRAW and nothing else. Hurtboxes come from
body_height/body_radius and every attack hitbox is built from world-metre
offsets around `position`; none of them consult it. Scaling there would give a
boss whose model does not match the capsule it is hit through -- the exact class
of defect that took three rounds to chase out of the strafes. The miniboss is
sized in its asset for the same reason (AssetManifest.h), and so is this.

Why the scale lives here and not in build_finalboss_pack.py
-----------------------------------------------------------
Every pose in make_finalboss_guard.py and make_finalboss_break.py is expressed
as an absolute world-metre target -- a fist at (0.340, -0.440, 0.060), a hip at
0.440 -- and each was solved against this character's measured 0.532 m arm and
0.359 m thigh. Scaling the rig BEFORE those run would invalidate every one of
them at once. Scaling after they are baked leaves the poses geometrically
identical and simply larger, which is what "make it bigger" means.

How it works, and what it takes with it
---------------------------------------
It sets one number: the armature object's scale, from Mixamo's 0.01 to
0.01 x FACTOR. merge_animations._bake_transforms folds that world matrix into
the mesh vertices and the rest bones, and _rescale_translation_keys rescales
every pose-bone location channel by the same factor -- so bone lengths, the
skin, and every clip's authored travel all scale together, with no per-clip
work. Verified below rather than assumed.

What does NOT follow automatically, and is updated by hand alongside this:
  * Enemy::body_height / body_radius (Swordman's FinalBoss branch) -- the
    hurtbox and the head marker, which are in world metres.
  * The attack hitbox capsules in AttackRegistry.cpp, likewise.
  * walk_speed / run_speed -- the clips' authored speeds scale with the rig, so
    the character speeds scale by the same factor to keep each clip playing at
    the rate it was tuned to.
"""

import sys

import bpy
from mathutils import Matrix, Vector

MAIN_ARM = "Armature"
MESH = "MutantMesh"
OUT = "/Users/long/Documents/3D/Model/pack_finalboss_scaled.blend"

# Mixamo's export scale, which every clip and every authored pose is measured in.
BASE_SCALE = 0.01

# Measured, not chosen by eye. Bind-pose mesh heights straight out of the
# shipped GLBs:
#
#     MiniBoss          2.627 m
#     Player (Sekiro)   1.877 m
#     FinalBoss         1.861 m   <- smallest boss in the game
#     Ashigaru          1.725 m
#
# 1.5 takes the final boss to 2.792 m: 0.165 m over the mini boss, which is
# "a little taller" and still reads as the same species of encounter rather than
# a different scale of one.
FACTOR = 1.5

# What the result has to come out at, within a millimetre. Asserted so a change
# to FACTOR that does not land where its comment claims fails here rather than
# in front of a player.
EXPECT_HEIGHT = 2.792
TOLERANCE = 0.002


def rest_pose():
    """Clear the pose, so a height measurement is the BIND height.

    The authoring passes leave the rig holding whatever they last solved, and
    detaching the action does not clear the pose bones. Measured against that
    leftover crouch the scale check read 2.624 m instead of 2.792 and failed --
    correctly, but for the wrong reason.
    """
    arm = bpy.data.objects[MAIN_ARM]
    if arm.animation_data:
        arm.animation_data.action = None
    for pb in arm.pose.bones:
        pb.matrix_basis = Matrix.Identity(4)
    bpy.context.view_layer.update()


def skinned_height():
    """Bind-pose height of the skin, in world metres."""
    rest_pose()
    obj = bpy.data.objects[MESH]
    dg = bpy.context.evaluated_depsgraph_get()
    ev = obj.evaluated_get(dg)
    mw = obj.matrix_world
    zs = [(mw @ v.co).z for v in ev.data.vertices]
    return min(zs), max(zs)


def main():
    arm = bpy.data.objects.get(MAIN_ARM)
    if arm is None:
        raise SystemExit("no %r in %s" % (MAIN_ARM, bpy.data.filepath))
    if MESH not in bpy.data.objects:
        raise SystemExit("no %r -- is this the final boss pack?" % MESH)

    before = arm.matrix_world.to_scale()
    if abs(before.x - BASE_SCALE) > 1e-6:
        raise SystemExit(
            "the armature is at scale %.4f, not Mixamo's %.4f. This script "
            "expects the authoring file untouched -- it is meant to run on "
            "pack_finalboss.blend, not on its own output." % (before.x, BASE_SCALE))

    lo_before, hi_before = skinned_height()

    arm.scale = (BASE_SCALE * FACTOR,) * 3
    bpy.context.view_layer.update()

    lo, hi = skinned_height()
    height = hi - lo
    if abs(height - EXPECT_HEIGHT) > TOLERANCE:
        raise RuntimeError(
            "scaling by %.3f gave %.3f m, not the %.3f m the comment claims. "
            "Either FACTOR or EXPECT_HEIGHT is stale."
            % (FACTOR, height, EXPECT_HEIGHT))

    print("SCALE_FINALBOSS " + repr({
        "factor": FACTOR,
        "object_scale": round(BASE_SCALE * FACTOR, 5),
        "height_before_m": round(hi_before - lo_before, 3),
        "height_after_m": round(height, 3),
        "feet_at_m": round(lo, 4),
        "vs_miniboss_m": round(height - 2.627, 3),
    }))

    bpy.ops.wm.save_as_mainfile(filepath=OUT)
    print("SAVED " + OUT)


if __name__ == "__main__":
    main()
