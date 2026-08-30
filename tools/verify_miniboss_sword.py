"""Measure what the greatsword actually does once the clips move it.

    blender -b ~/Documents/3D/Model/pack_miniboss.blend \
        --python tools/verify_miniboss_sword.py

tools/add_miniboss_sword.py can only check the REST pose: that the hilt sits in
the fist and that the hold survived the transfer. Neither says anything about
the thing a 2.5 m weapon on a 2.6 m character is actually likely to get wrong,
which is sweeping through the floor on a one-handed swing. These clips are
Mixamo's sword-and-shield set, authored for a one-handed arming sword; nothing
in them knows the blade got 1.8 m long.

So this poses the character through every clip that swings and reports, per
clip, how far the lowest point of the sword goes below the character's own feet.
A negative `below_feet` is blade under the floor.

Read it as a budget, not a pass/fail. Some ground contact on a big downward
swing is normal and reads as weight; a blade that spends a whole clip half a
metre under the terrain does not. If it needs to come down, SCALE_BONE in
add_miniboss_sword.py is the one knob -- and the grip stops fitting the fist if
it goes much below the hand ratio, so prefer re-authoring the swing.

Frames are sampled at the ends of the interior range only. The last keyframe of
a raylib clip wraps to the first, and Blender will happily evaluate a frame the
runtime never plays.
"""

import os

import bpy

# Lifted out of the source rather than imported: merge_animations.py calls
# main() at module scope, so importing it would run a full GLB export.
_SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "merge_animations.py")
_ns = {}
exec(open(_SRC).read().split("\ndef _clip_name")[0], _ns)
CLIP_NAMES = _ns["CLIP_NAMES"]

MAIN_ARM = "Armature"
SWORD = "MiniBoss_Sword"
BODY = "mesh"

# Every clip whose whole point is to move the weapon, plus the idle and the
# locomotion as a baseline. Names are what the runtime resolves.
#
# These are the GREATSWORD pack's names. The list used to hold the player set
# this character borrowed before tools/build_miniboss_pack.py replaced it
# (Slash, Block, Kick ...), none of which is in the file any more -- so every
# row printed "(clip not in this file)" and the tool silently measured nothing.
CLIPS = ("Idle", "Walk", "Run", "Guard", "GuardWalk",
         "Attack", "Attack_2", "Attack_3", "Attack_H",
         "Combo_1", "Combo_2", "Combo_3")


def main():
    arm = bpy.data.objects[MAIN_ARM]
    sword = bpy.data.objects[SWORD]
    body = bpy.data.objects[BODY]
    by_clip = {v: k for k, v in CLIP_NAMES.items()}

    if arm.animation_data is None:
        arm.animation_data_create()

    rows = []
    for clip in CLIPS:
        # build_miniboss_pack names a clip armature after the clip itself;
        # CLIP_NAMES is only needed for the two rows inherited from the player
        # pack, whose armatures still carry an `Armature.NNN` name.
        src = bpy.data.objects.get(clip) or bpy.data.objects.get(by_clip.get(clip, ""))
        if src is None or not (src.animation_data and src.animation_data.action):
            rows.append((clip, None, None, None))
            continue
        action = src.animation_data.action
        arm.animation_data.action = action
        if src.animation_data.action_slot is not None:
            arm.animation_data.action_slot = src.animation_data.action_slot

        f0, f1 = (int(v) for v in action.frame_range)
        lowest, feet, reach = 1e9, 1e9, 0.0
        for f in range(f0, f1):          # f1 excluded: it wraps to f0 at runtime
            bpy.context.scene.frame_set(f)
            dg = bpy.context.evaluated_depsgraph_get()
            for obj, is_sword in ((sword, True), (body, False)):
                ev = obj.evaluated_get(dg)
                me = ev.to_mesh()
                zs = [(ev.matrix_world @ v.co).z for v in me.vertices]
                if is_sword:
                    lowest = min(lowest, min(zs))
                    reach = max(reach, max(zs))
                else:
                    feet = min(feet, min(zs))
                ev.to_mesh_clear()
        rows.append((clip, lowest, feet, reach))

    arm.animation_data.action = None
    print("clip              sword_low   feet    below_feet   reach")
    worst = None
    for clip, low, feet, reach in rows:
        if low is None:
            print("%-16s  (clip not in this file)" % clip)
            continue
        below = low - feet
        print("%-16s  %8.3f  %6.3f  %10.3f  %6.3f" % (clip, low, feet, below, reach))
        if worst is None or below < worst[1]:
            worst = (clip, below)
    if worst:
        print("VERIFY_WORST %s %.3f m below the feet" % worst)


if __name__ == "__main__":
    main()
