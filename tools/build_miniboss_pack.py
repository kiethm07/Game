"""Replace the miniboss's borrowed clip set with a greatsword pack of its own.

    blender -b ~/Documents/3D/Model/pack_miniboss.blend \
        --python tools/build_miniboss_pack.py -- --save

Until now the Blood Knight animated off the PLAYER's 60 clips, retargeted --
sword-and-shield motion on a character carrying a 2.5 m two-hander. This swaps
in Mixamo's Great Sword pack and keeps exactly two clips from the old set.

Why this is an import and not a second retarget
-----------------------------------------------
tools/retarget_miniboss.py rebuilt this character's rest skeleton onto Mixamo's
convention, and these downloads are stock Mixamo rigs. Measured before writing
this: the two rest poses agree to a MEDIAN OF 0.00 deg across all 65 shared
bones. Mixamo clips are rotation-only, so a clip authored against one of those
rest poses means the same thing against the other, and the actions transplant
with no correction at all.

The one exception is the ankles, which sit 20.9 deg apart (and the toes 6.0),
because the retarget rebuilt the foot off this model's own joints rather than
off Mixamo's. That is a real error and it lands entirely on the feet. It is
accepted rather than corrected: RUNTIME_ANKLE_DEVIATION documents it, and on a
booted, armoured character at gameplay distance it does not read.

What is kept, and why only these two
------------------------------------
`Fall` and `PostureBreak` have no counterpart in the greatsword pack. Its jump
clips travel or land; neither is the sustained falling loop the runtime needs,
and the player pack's Fall is one (its toes hold 0.35 clear of the floor on
every frame). PostureBreak is the Paladin/player pack's `FallToKneel`, which is
what the deathblow window has always shown and what was asked for here.

Everything else is measured, not trusted to a filename
------------------------------------------------------
Mixamo ships four files called "great sword strafe" and five called "great sword
impact". SOURCES below records what each one actually does -- net hip travel in
metres and the hip height band -- because that is how each was identified:

  * forward is -Y, calibrated off Run's own travel
  * left is up x forward = +X, so +X strafes are LEFT and -X are RIGHT
  * of the four strafes two run at ~1.1 m/s and two at ~2.4-3.0; the slow pair
    is taken, because they have to sit beside a 1.07 m/s walk
  * of the five impacts, (3) holds the hips at 0.98-1.00 -- a flinch that keeps
    its feet -- while (4) and (5) drop to 0.48-0.53, which are knockdowns

StrafeForward gets no clip of its own. It never had one: the desc table points
it at Walk, the same way the ashigaru's does.
"""

import os
import sys

import bpy

MAIN_ARM = "Armature"
CURATED = "/Users/long/Documents/3D/Model/animation pack mini boss"
GREATSWORD = "/Users/long/Documents/3D/Model/Great Sword Pack"

# Clips kept from the borrowed set. Actions, not armatures -- the armatures they
# arrived on are deleted with the rest.
KEEP = {"Fall": "Fall", "FallToKneel": "PostureBreak"}

# clip name -> (file, what it was measured doing)
SOURCES = {
    "Idle":        ("great sword idle.fbx",             "60f, static, hips 0.99-1.00"),
    "Walk":        ("great sword walk.fbx",             "41f, -Y 1.456 m, 1.07 m/s"),
    "Run":         ("Great Sword Run.fbx",              "18f, -Y 2.421 m, 4.03 m/s"),
    "StrafeBack":  ("great sword walk (2).fbx",         "39f, +Y 1.287 m, 0.99 m/s"),
    "StrafeLeft":  ("great sword strafe.fbx",           "33f, +X 1.206 m, 1.10 m/s"),
    "StrafeRight": ("great sword strafe (2).fbx",       "35f, -X 1.384 m, 1.19 m/s"),
    "Attack":      ("great sword attack.fbx",           "36f, in place"),
    # The mini boss's normal swing. Added after the first build, so it arrives
    # through tools/add_miniboss_clip.py rather than a full rebuild -- the row
    # lives here anyway so a rebuild from scratch still produces it.
    "Attack_H":    ("standing melee attack horizontal.fbx", "48f, in place"),
    "Attack_2":    ("great sword slash.fbx",            "38f, in place"),
    "Attack_3":    ("great sword slash (3).fbx",        "55f, in place"),
    "Combo_1":     ("standing melee combo attack ver. 1.fbx", "140f, -Y 1.529 m"),
    "Combo_2":     ("standing melee combo attack ver. 2.fbx", "126f, in place"),
    "Combo_3":     ("standing melee combo attack ver. 3.fbx", "82f, in place"),
    "HitReact":    ("great sword impact (3).fbx",       "38f, hips 0.978-0.998"),
    "Death":       ("two handed sword death.fbx",       "72f, hips 0.994 -> 0.117"),
}

# Documented, not corrected -- see the module docstring.
RUNTIME_ANKLE_DEVIATION = 20.9


def _find(name):
    for d in (CURATED, GREATSWORD):
        p = os.path.join(d, name)
        if os.path.exists(p):
            return p
    raise SystemExit("no such clip file: %r" % name)


def strip_old_clips(main):
    """Delete every clip armature, having first rescued the actions in KEEP."""
    rescued = {}
    for obj in bpy.data.objects:
        if obj.type != "ARMATURE" or obj is main:
            continue
        act = obj.animation_data.action if obj.animation_data else None
        if act is None:
            continue
        # merge_animations names a clip after its ARMATURE, so that is where the
        # old clip's identity lives, not in the action's own name.
        from_name = obj.get("clip_name") or obj.name
        rescued[from_name] = (obj, act)

    keep_objs = {}
    here = os.path.dirname(os.path.abspath(__file__))
    ns = {}
    exec(open(os.path.join(here, "merge_animations.py")).read()
         .split("\ndef _clip_name")[0], ns)
    names = ns["CLIP_NAMES"]

    for obj_name, (obj, act) in list(rescued.items()):
        clip = names.get(obj.name, obj.name)
        if clip in KEEP:
            keep_objs[KEEP[clip]] = obj
            act.use_fake_user = True

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
    return removed, sorted(keep_objs)


def import_clip(clip, filename):
    """Import one FBX and leave its armature named for the clip it carries."""
    before = set(o.name for o in bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=_find(filename))
    new = [bpy.data.objects[n] for n in set(o.name for o in bpy.data.objects) - before]
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
    # merge_animations reads the clip name off the armature, and CLIP_NAMES only
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
            raise SystemExit("%r collides with an armature already in the file" % clip)
        import_clip(clip, filename)
        built.append((clip, filename, measured))

    clips = sorted(o.name for o in bpy.data.objects
                   if o.type == "ARMATURE" and o is not main_arm)
    print("BUILD_MINIBOSS_PACK " + repr({
        "removed_armatures": removed,
        "kept": kept,
        "imported": len(built),
        "clips": clips,
        "meshes": [o.name for o in bpy.data.objects
                   if o.type == "MESH" and o.parent is main_arm],
    }))
    for clip, filename, measured in built:
        print("   %-12s <- %-42s %s" % (clip, filename, measured))
    return clips


if __name__ == "__main__":
    main()
    if "--save" in sys.argv:
        bpy.ops.wm.save_mainfile()
        print("SAVED " + bpy.data.filepath)
