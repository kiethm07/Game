"""Replace the kimono's borrowed clip set with a nodachi pack of its own.

    blender -b ~/Documents/3D/Model/pack_kimono.blend \
        --python tools/build_kimono_pack.py -- --save

Step 3 of tools/rebuild_kimono.sh. tools/retarget_kimono.py left this character
skinned to the Mixamo rig and still carrying the PLAYER's 60 clips -- katana
motion on a character holding a two-handed nodachi. This swaps in Mixamo's Great
Sword pack and keeps exactly two of the old clips.

An import, not a second retarget
--------------------------------
retarget_kimono.py rebuilt this character's rest skeleton onto Mixamo's
convention, and these downloads are stock Mixamo rigs. Mixamo clips are
rotation-only, so a clip authored against one of those rest poses means the same
thing against the other and the actions transplant with no correction.

Everything is measured, not trusted to a filename
-------------------------------------------------
Mixamo ships four files called "great sword strafe", five called "great sword
impact" and three called "great sword blocking". SOURCES records what each one
was measured DOING -- net hip travel and the hip height band over the clip --
because that is how each was chosen. Forward is -Y and left is +X, both
calibrated off Run's own travel.

The measurements that decided the non-obvious ones:

  * of the four strafes, two run at ~1.2 m/s and two at 2.4-3.1; the slow pair
    is taken, because they sit beside a 1.09 m/s walk
  * of the three blockings, (2) is the only one that HOLDS -- 30 frames with
    the hips flat at 0.762-0.766. The other two run 0.764 -> 0.994, which are
    the transitions into and out of it, not the stance
  * of the five impacts, the unnumbered one holds the hips at 0.759-0.765,
    which is the guard's own height, so it is the flinch that belongs on a
    RAISED guard; (3) holds 0.978-0.998, a flinch that keeps its feet from
    standing, which is the unblocked one. (4) and (5) drop to ~0.48 and are
    knockdowns
  * of the two deaths, the brief asks for a collapse face down: (2) travels
    -Y 1.262 (forward, this rig faces -Y) while the unnumbered one travels
    +Y 0.856 and lands on its back
  * of the three combos, COMBO_1 in the brief is a grounded cleave string and
    COMBO_2 a lunge-and-thrust, so they take ver. 2 (127f, in place, hips
    dipping to 0.754) and ver. 1 (141f, advancing 1.531 m) respectively

Two clips in the brief have no source and are NOT invented here
---------------------------------------------------------------
GUARD_WALK and STRAFE_FWD. The greatsword pack ships neither a guarded walk nor
a forward dash, and both fall back the way the other two enemies' already do --
SwordmanAnimator's table points GuardWalk at Guard and StrafeForward at Walk.
Authoring them is tools/make_miniboss_guard.py's job on that character and could
be done here too; it is left undone rather than done badly, and the fallback is
what the ashigaru has always shipped.
"""

import os
import sys

import bpy

MAIN_ARM = "Armature"
CURATED = os.path.expanduser("~/Documents/3D/Model/animation pack mini boss")
GREATSWORD = os.path.expanduser("~/Documents/3D/Model/Great Sword Pack")

# Clips kept from the borrowed set. The greatsword pack has no counterpart for
# either: its jump clips travel or land rather than hovering, and nothing in it
# is the kneel the deathblow window has always shown.
KEEP = {"Fall": "Fall", "FallToKneel": "PostureBreak"}

# clip name -> (file, what it was measured doing)
SOURCES = {
    "Idle":        ("great sword idle (2).fbx",   "112f, in place, hips 0.984-0.994"),
    "Walk":        ("great sword walk.fbx",       "42f, -Y 1.485 m, 1.09 m/s"),
    "Run":         ("Great Sword Run.fbx",        "18f, -Y 2.421 m, 4.03 m/s"),
    "StrafeBack":  ("great sword walk (2).fbx",   "40f, +Y 1.308 m, 1.01 m/s"),
    "StrafeLeft":  ("great sword strafe.fbx",     "34f, +X 1.269 m, 1.15 m/s"),
    "StrafeRight": ("great sword strafe (2).fbx", "36f, -X 1.454 m, 1.25 m/s"),
    "Guard":       ("great sword blocking (2).fbx", "30f, in place, hips 0.762-0.766"),
    "GuardImpact": ("great sword impact.fbx",     "27f, in place, hips 0.759-0.765"),
    "HitReact":    ("great sword impact (3).fbx", "39f, in place, hips 0.978-0.998"),
    "Death":       ("two handed sword death (2).fbx", "79f, -Y 1.262 m, hips 1.027 -> 0.155"),
    "Attack":      ("great sword slash.fbx",      "39f, in place, hips 0.857-0.996"),
    "Attack_H":    ("standing melee attack horizontal.fbx", "73f, in place, hips 0.807-0.913"),
    "Combo_1":     ("standing melee combo attack ver. 2.fbx", "127f, in place, hips 0.754-0.941"),
    "Combo_2":     ("standing melee combo attack ver. 1.fbx", "141f, -Y 1.531 m"),
}


def _find(name):
    for d in (CURATED, GREATSWORD):
        p = os.path.join(d, name)
        if os.path.exists(p):
            return p
    raise SystemExit("no such clip file: %r" % name)


def strip_old_clips(main):
    """Delete every clip armature, having first rescued the actions in KEEP."""
    here = os.path.dirname(os.path.abspath(__file__))
    ns = {}
    exec(open(os.path.join(here, "merge_animations.py")).read()
         .split("\ndef _clip_name")[0], ns)
    names = ns["CLIP_NAMES"]

    keep_objs = {}
    for obj in bpy.data.objects:
        if obj.type != "ARMATURE" or obj is main:
            continue
        anim = obj.animation_data
        if not (anim and anim.action):
            continue
        # merge_animations names a clip after its ARMATURE, so that is where
        # the old clip's identity lives, not in the action's own name.
        clip = names.get(obj.name, obj.name)
        if clip in KEEP:
            keep_objs[KEEP[clip]] = obj
            anim.action.use_fake_user = True

    missing = set(KEEP.values()) - set(keep_objs)
    if missing:
        raise SystemExit("could not find the clips to keep: %s" % sorted(missing))

    removed = 0
    for obj in list(bpy.data.objects):
        if obj.type != "ARMATURE" or obj is main or obj in keep_objs.values():
            continue
        bpy.data.objects.remove(obj, do_unlink=True)
        removed += 1

    for clip, obj in keep_objs.items():
        obj.name = clip
        obj.animation_data.action.name = clip
    return removed, sorted(keep_objs)


def import_clip(clip, filename):
    """Import one FBX and leave its armature named for the clip it carries."""
    before = {o.name for o in bpy.data.objects}
    bpy.ops.import_scene.fbx(filepath=_find(filename))
    new = [bpy.data.objects[n] for n in {o.name for o in bpy.data.objects} - before]
    arms = [o for o in new if o.type == "ARMATURE"]
    if len(arms) != 1:
        raise SystemExit("%s produced %d armatures" % (filename, len(arms)))
    arm = arms[0]
    # Mixamo animation-only downloads carry no skin, but a pack export can; any
    # mesh that did come in is not this character and must not reach the GLB.
    for o in new:
        if o.type == "MESH":
            bpy.data.objects.remove(o, do_unlink=True)
    if not (arm.animation_data and arm.animation_data.action):
        raise SystemExit("%s holds no action" % filename)
    arm.animation_data.action.use_fake_user = True
    # merge_animations reads the clip name off the ARMATURE, and CLIP_NAMES only
    # maps `Armature.NNN` keys -- so naming it here is what makes the exported
    # animation come out as `Idle` rather than as the player table's guess for
    # whatever import index it happened to land on.
    arm.name = clip
    arm.animation_data.action.name = clip
    return arm


def main():
    main_arm = bpy.data.objects[MAIN_ARM]
    removed, kept = strip_old_clips(main_arm)

    built = []
    for clip, (filename, measured) in SOURCES.items():
        if bpy.data.objects.get(clip):
            raise SystemExit("%r collides with an armature already in the file"
                             % clip)
        import_clip(clip, filename)
        built.append({"clip": clip, "file": filename, "measured": measured})

    clips = sorted(o.name for o in bpy.data.objects
                   if o.type == "ARMATURE" and o is not main_arm)
    print("KIMONO_PACK " + repr({
        "removed_borrowed": removed, "kept": kept,
        "imported": len(built), "clips": clips,
        "skin_meshes": len([o for o in bpy.data.objects
                            if o.type == "MESH" and o.parent is main_arm]),
        "bones": len(main_arm.data.bones),
    }))
    for row in built:
        print("   %-12s %-42s %s" % (row["clip"], row["file"], row["measured"]))

    if "--save" in sys.argv:
        bpy.ops.wm.save_as_mainfile()
        print("SAVED " + bpy.data.filepath)


if __name__ == "__main__":
    main()
