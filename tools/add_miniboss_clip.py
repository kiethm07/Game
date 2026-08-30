"""Import one clip from build_miniboss_pack.SOURCES into an already-built pack.

    blender -b ~/Documents/3D/Model/pack_miniboss.blend \
        --python tools/add_miniboss_clip.py -- Attack_H --save

tools/build_miniboss_pack.py is a REPLACEMENT: strip_old_clips() deletes every
clip armature that is not in KEEP, and the guard set that
tools/make_miniboss_guard.py bakes afterwards is not in KEEP. So re-running it
to pick up one new row in SOURCES would silently throw Guard, GuardWalk and
GuardImpact away and they would have to be re-authored.

This adds a row instead. It imports through build_miniboss_pack's own
import_clip(), so the naming convention merge_animations depends on -- the
armature carries the clip name -- is stated in exactly one place, and refuses to
touch a clip the file already holds.
"""

import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import build_miniboss_pack as pack


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    wanted = [a for a in argv if not a.startswith("--")]
    if not wanted:
        raise SystemExit("usage: ... -- <ClipName> [ClipName ...] [--save]")

    added = []
    for clip in wanted:
        if clip not in pack.SOURCES:
            raise SystemExit("%r is not a row in build_miniboss_pack.SOURCES" % clip)
        if bpy.data.objects.get(clip):
            raise SystemExit("%r is already in this file" % clip)

        filename, measured = pack.SOURCES[clip]
        arm = pack.import_clip(clip, filename)
        action = arm.animation_data.action
        start, end = action.frame_range
        added.append((clip, filename, int(end - start) + 1, measured))

    print("ADD_MINIBOSS_CLIP " + repr({
        "added": [c for c, _, _, _ in added],
        "clips": sorted(o.name for o in bpy.data.objects
                        if o.type == "ARMATURE" and o.name != pack.MAIN_ARM),
    }))
    for clip, filename, frames, measured in added:
        print("   %-12s <- %-42s %df imported (row says: %s)"
              % (clip, filename, frames, measured))


if __name__ == "__main__":
    main()
    if "--save" in sys.argv:
        bpy.ops.wm.save_mainfile()
        print("SAVED " + bpy.data.filepath)
