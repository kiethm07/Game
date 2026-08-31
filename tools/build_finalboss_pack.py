"""Give the final boss its clip pack, one armature per clip.

    blender -b ~/Documents/3D/Model/finalboss.blend \
        --python tools/build_finalboss_pack.py -- --save

Reads finalboss.blend and writes pack_finalboss.blend, leaving the hand-made
source file untouched -- the same split as miniboss.blend -> pack_miniboss.blend.
Re-running is a REPLACEMENT: it drops every clip in the file and re-imports, so
it deletes anything the make_finalboss_*.py scripts authored. Rebuild in the
order rebuild_finalboss.sh runs them.

The model
--------
Mixamo's Mutant, already rigged when it arrived -- so unlike the miniboss there
is no retarget pass here at all. The clips in `animation pack final boss` were
downloaded ON this character, and a fresh import of each was measured against
the file's own rig before this table was written: all eight carry the same 37
bones with the same names, so the actions transplant exactly, with no
correction and no ankle deviation to document.

That 37 is not a mistake and not a subset that needs filling in. The Mutant
ships with no left-hand fingers (the big arm is one solid mitt) and no spine
above Spine2 but Neck/Head. It is the rig the clips were authored against, so
it is the rig the pack keeps.

What each file actually is
--------------------------
Identified by measurement, not by filename -- net hip travel in metres, the hip
height band, and how far each hand travels along its own path. Two of the eight
are near-twins by name and would otherwise be easy to swap.

Four clips ship as themselves. The other four are SOURCES: raw material the
authoring scripts cut up, and they must not reach the GLB. They are named with
a `src_` prefix, and tools/make_finalboss_attacks.py -- the last authoring pass
-- deletes them once nothing needs them any more. merge_animations.py exports
every armature in the file that carries an action, so a src_ holder left behind
is a clip shipped by accident.

`Roar` is kept even though SwordmanAnimator names no state for it. It costs one
clip in the pack and it is the obvious thing for a boss intro to want; nothing
breaks if it goes unused, because clip resolution is by name.
"""

import os
import sys

import bpy

MAIN_ARM = "Armature"
MESH = "MutantMesh"
PACK = "/Users/long/Documents/3D/Model/animation pack final boss"
OUT = "/Users/long/Documents/3D/Model/pack_finalboss.blend"

# clip name -> (file, what a fresh import of it was measured doing)
#
# Frame counts are at 30 fps, which is the scene rate finalboss.blend carries
# and the rate Mixamo authored these at.
SOURCES = {
    "Idle":      ("mutant idle (2).fbx",   "160f, in place, hips 0.802-0.829"),
    "Walk":      ("mutant walking.fbx",    "44f, -Y 1.739 m, 1.19 m/s"),
    "Run":       ("mutant run.fbx",        "27f, -Y 1.911 m, 2.12 m/s"),
    "Roar":      ("mutant roaring.fbx",    "163f, in place, toes lift to 0.297"),
    # Raw material. Deleted by make_finalboss_attacks.py once consumed.
    "src_Swipe": ("mutant swiping.fbx",    "81f, in place, L hand path 8.04 > R 6.36"),
    "src_Punch": ("mutant punch.fbx",      "34f, in place, R hand path 4.51 > L 2.66"),
    "src_Jump":  ("mutant jump attack.fbx", "112f, -Y 1.594 m, hips peak 2.391"),
    "src_Dying": ("mutant dying.fbx",      "139f, +Y 0.865 m, hips 0.807 -> 0.154"),
}

# Everything the authoring scripts consume and nothing ships.
SCRATCH_PREFIX = "src_"


def clear_old_clips():
    """Drop every armature but the skinned one, and every action in the file.

    finalboss.blend arrives with five actions already imported (the Mutant's own
    2-frame bind pose, plus swiping, punch, a jump attack and idle) sitting
    directly on the main armature rather than on holders. None of them are in
    the shape the rest of the pipeline reads, and all five are re-imported below
    under names that mean something, so they go.
    """
    removed_objs = 0
    for obj in list(bpy.data.objects):
        if obj.type == "ARMATURE" and obj.name != MAIN_ARM:
            bpy.data.objects.remove(obj, do_unlink=True)
            removed_objs += 1

    main = bpy.data.objects[MAIN_ARM]
    if main.animation_data:
        main.animation_data.action = None
        for track in list(main.animation_data.nla_tracks):
            main.animation_data.nla_tracks.remove(track)

    removed_acts = 0
    for act in list(bpy.data.actions):
        act.use_fake_user = False
        bpy.data.actions.remove(act)
        removed_acts += 1
    return removed_objs, removed_acts


def import_clip(clip, filename):
    """Import one FBX and leave its armature named for the clip it carries."""
    path = os.path.join(PACK, filename)
    if not os.path.exists(path):
        raise SystemExit("no such clip file: %s" % path)

    before = {o.name for o in bpy.data.objects}
    bpy.ops.import_scene.fbx(filepath=path)
    new = [bpy.data.objects[n] for n in {o.name for o in bpy.data.objects} - before]

    arms = [o for o in new if o.type == "ARMATURE"]
    if len(arms) != 1:
        raise SystemExit("%s produced %d armatures" % (filename, len(arms)))
    holder = arms[0]

    # A Mixamo animation-only download carries no skin, but a pack export can;
    # any mesh that did come in is a second copy of this character and must not
    # reach the GLB.
    for o in new:
        if o.type == "MESH":
            bpy.data.objects.remove(o, do_unlink=True)

    if not (holder.animation_data and holder.animation_data.action):
        raise SystemExit("%s holds no action" % filename)

    bones = {b.name for b in holder.data.bones}
    want = {b.name for b in bpy.data.objects[MAIN_ARM].data.bones}
    if bones != want:
        raise SystemExit(
            "%s carries a different skeleton: %d bones against the model's %d "
            "(only in clip: %s / only in model: %s). The actions cannot "
            "transplant." % (filename, len(bones), len(want),
                             sorted(bones - want)[:4], sorted(want - bones)[:4]))

    act = holder.animation_data.action
    act.use_fake_user = True
    act.name = clip
    # merge_animations reads a clip's name off its ARMATURE, falling through
    # CLIP_NAMES (which only maps the player pack's `Armature.NNN` keys) to the
    # object name -- so naming the object here is what makes the exported
    # animation come out as `Idle`.
    holder.name = clip
    return holder, int(act.frame_range[1] - act.frame_range[0] + 1)


def main():
    if MAIN_ARM not in bpy.data.objects:
        raise SystemExit("no %r in %s" % (MAIN_ARM, bpy.data.filepath))
    if MESH not in bpy.data.objects:
        raise SystemExit("no %r in %s -- is this the final boss file?"
                         % (MESH, bpy.data.filepath))

    removed_objs, removed_acts = clear_old_clips()

    built = []
    for clip, (filename, measured) in SOURCES.items():
        if bpy.data.objects.get(clip):
            raise SystemExit("%r collides with an object already in the file" % clip)
        _, frames = import_clip(clip, filename)
        built.append((clip, filename, frames, measured))

    ships = sorted(c for c, _, _, _ in built if not c.startswith(SCRATCH_PREFIX))
    scratch = sorted(c for c, _, _, _ in built if c.startswith(SCRATCH_PREFIX))
    print("BUILD_FINALBOSS_PACK " + repr({
        "removed_armatures": removed_objs,
        "removed_actions": removed_acts,
        "imported": len(built),
        "ships": ships,
        "scratch": scratch,
        "bones": len(bpy.data.objects[MAIN_ARM].data.bones),
        "meshes": [o.name for o in bpy.data.objects
                   if o.type == "MESH" and o.parent
                   and o.parent.name == MAIN_ARM],
    }))
    for clip, filename, frames, measured in built:
        print("   %-11s <- %-24s %4df  %s" % (clip, filename, frames, measured))


if __name__ == "__main__":
    main()
    if "--save" in sys.argv:
        bpy.ops.wm.save_as_mainfile(filepath=OUT)
        print("SAVED " + OUT)
