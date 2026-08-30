"""Drop a clip's hips until the body rests on the floor its feet started on.

    blender -b ~/Documents/3D/Model/pack_miniboss.blend \
        --python tools/plant_clip_on_floor.py -- Death --save

Why this is needed at all
-------------------------
Mixamo clips are rotation-only EXCEPT for the hips, whose translation is an
absolute offset in metres (see the retarget notes in build_miniboss_pack.py).
Rotation transplants onto any rig; an absolute translation does not. This
character is 2.589 m tall with its hips at 1.273 m, where the rig
`two handed sword death.fbx` was authored for stands about a fifth shorter --
so the clip's authored 0.877 m hip drop, which laid ITS character out flat, only
gets ours most of the way down. Measured: the hips end at 0.350 m when the same
pose on this body needs roughly 0.15, and the corpse ends up hovering 0.09 to
0.15 m clear of the ground for the whole second half of the clip. On a body
lying flat that gap is the whole silhouette, which is what reads as floating.

What it does
------------
Per frame: skin the body, find its lowest vertex, and translate the hips down by
however far that sits above `--floor`. Translating the hips moves the entire
skeleton rigidly, so the correction is exact in one pass and needs no solve --
and because it is measured per frame it follows the fall instead of imposing a
constant offset that would sink the character while they are still standing.

By default it will only ever LOWER a frame, never lift one. A clip that leaves
the body under the floor is a different defect (the mini boss's PostureBreak
kneels 0.08 m into it) and lifting is the wrong reflex for any clip with a
genuine airborne phase, so raising is opt-in via --raise.

`--floor` defaults to the clip's own first frame -- wherever the character's
feet were standing before the clip moved them, which is by definition the
surface the runtime has them standing on.
"""

import sys

import bpy
from mathutils import Vector

MAIN_ARM = "Armature"
BODY = "mesh"
HIPS = "mixamorig:Hips"


def _lowest(body, depsgraph):
    """Lowest point of the skinned body, in world space (metres)."""
    ev = body.evaluated_get(depsgraph)
    me = ev.to_mesh()
    low = min((ev.matrix_world @ v.co).z for v in me.vertices)
    ev.to_mesh_clear()
    return low


def plant(clip, floor=None, allow_raise=False):
    arm = bpy.data.objects[MAIN_ARM]
    body = bpy.data.objects[BODY]
    src = bpy.data.objects.get(clip)
    if src is None or not (src.animation_data and src.animation_data.action):
        raise SystemExit("no clip armature named %r in this file" % clip)

    if arm.animation_data is None:
        arm.animation_data_create()
    arm.animation_data.action = src.animation_data.action
    if src.animation_data.action_slot is not None:
        arm.animation_data.action_slot = src.animation_data.action_slot

    action = src.animation_data.action
    f0, f1 = (int(v) for v in action.frame_range)
    scene = bpy.context.scene

    # Pass 1: measure the untouched clip. Separate from the write pass so no
    # frame is ever measured against keys this run has already changed.
    lows = {}
    for f in range(f0, f1 + 1):
        scene.frame_set(f)
        lows[f] = _lowest(body, bpy.context.evaluated_depsgraph_get())

    if floor is None:
        floor = lows[f0]

    # World +Z into the armature's own space, which is where a pose bone's
    # matrix lives. Carries both the Mixamo import's 90 deg X rotation and its
    # 0.01 scale, so the correction does not have to know about either.
    to_arm = arm.matrix_world.to_3x3().inverted()

    pb = arm.pose.bones[HIPS]
    moved = 0
    worst = 0.0
    for f in range(f0, f1 + 1):
        delta = floor - lows[f]
        if delta > 0.0 and not allow_raise:
            delta = 0.0
        if abs(delta) < 1e-6:
            continue
        scene.frame_set(f)
        m = pb.matrix.copy()
        m.translation = m.translation + (to_arm @ Vector((0.0, 0.0, delta)))
        pb.matrix = m
        bpy.context.view_layer.update()
        pb.keyframe_insert("location", frame=f)
        moved += 1
        worst = min(worst, delta) if delta < 0 else worst

    # Pass 3: prove it. The hips translate the whole skeleton rigidly, so one
    # pass should be exact -- if it is not, something about the bone space is
    # wrong and it must not be saved.
    residual = 0.0
    for f in range(f0, f1 + 1):
        scene.frame_set(f)
        after = _lowest(body, bpy.context.evaluated_depsgraph_get())
        if lows[f] > floor or allow_raise:
            residual = max(residual, abs(after - floor))
    arm.animation_data.action = None

    print("PLANT %-13s floor=%+.3f  frames %d-%d, %d corrected, "
          "largest drop %.3f m, residual %.4f m"
          % (clip, floor, f0, f1, moved, -worst, residual))
    if residual > 0.005:
        raise SystemExit("plant did not converge on %s (residual %.4f m)"
                         % (clip, residual))


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    clips = [a for a in argv if not a.startswith("--")]
    if not clips:
        raise SystemExit("usage: ... -- <ClipName> [ClipName ...] "
                         "[--floor=Z] [--raise] [--save]")
    floor = None
    for a in argv:
        if a.startswith("--floor="):
            floor = float(a.split("=", 1)[1])
    allow_raise = "--raise" in argv

    for clip in clips:
        plant(clip, floor, allow_raise)


if __name__ == "__main__":
    main()
    if "--save" in sys.argv:
        bpy.ops.wm.save_mainfile()
        print("SAVED " + bpy.data.filepath)
