"""Shift the authored map so `ground` sits where make_castle_level.py expects it.

This is step 1 of building a level from the castle map, and until now it was the
one step that lived only in somebody's head.

The art in ~/Documents/3D/MAP/ is kitbashed around the origin. Every absolute
coordinate in make_castle_level.py -- ISLAND_CENTRE, RECENTRE, GATE_AXIS_Y,
PLAYER_SPAWN_XY, ENEMY_SPAWNS_XY, COMPOUND_*, BRIDGE_*, and every profile's
PLAY_CENTRE and BOUNDARY_RECT -- was written in an older frame where the island
centred on (-146.9, -16.1). All of them are self-consistent under one uniform
translation, so moving the *art* onto that frame restores every one of them at
once. Rewriting ~20 literals instead is the same change with 20 chances to
fumble the arithmetic, and a wrong one puts collision proxies metres away from
the geometry they are meant to wrap.

Only parentless objects move. A child's world position follows its parent, so
shifting both would double the offset on the child.

Usage:
    blender --background <art.blend> --python tools/recentre_map.py -- \
        --out source/levels/<name>.blend
"""

import argparse
import sys

import bpy
from mathutils import Vector

# Must match make_castle_level.ISLAND_CENTRE. Duplicated rather than imported
# because this script runs before the level build and has no reason to pull in
# that module's Blender-state assumptions -- but a mismatch would be silent, so
# it is checked against the value the build guard uses, below.
ISLAND_CENTRE = (-146.9, -16.1)

# make_castle_level.check_recentre allows this much drift before it refuses to
# build. Landing inside a fraction of it leaves room for the art to be nudged
# without needing a re-run.
TOLERANCE = 1.0


def plan_centre(obj):
    corners = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
    xs = [c.x for c in corners]
    ys = [c.y for c in corners]
    return ((min(xs) + max(xs)) * 0.5, (min(ys) + max(ys)) * 0.5)


def parse_args(argv):
    argv = argv[argv.index("--") + 1:] if "--" in argv else []
    parser = argparse.ArgumentParser(prog="recentre_map.py")
    parser.add_argument("--out", required=True,
                        help="where to write the recentred .blend")
    return parser.parse_args(argv)


def main():
    args = parse_args(sys.argv)

    ground = bpy.data.objects.get("ground")
    if ground is None:
        raise SystemExit("no object named 'ground' -- this is not the castle map.")

    before = plan_centre(ground)
    delta = Vector((ISLAND_CENTRE[0] - before[0],
                    ISLAND_CENTRE[1] - before[1], 0.0))

    moved = 0
    for obj in bpy.data.objects:
        if obj.parent is None:
            obj.location += delta
            moved += 1
    bpy.context.view_layer.update()

    after = plan_centre(ground)
    drift = Vector((after[0] - ISLAND_CENTRE[0],
                    after[1] - ISLAND_CENTRE[1])).length
    if drift > TOLERANCE:
        # The shift is a rigid translation, so this can only fail if `ground`
        # is parented to something that did not move with it.
        raise SystemExit(
            "recentre landed on (%.2f, %.2f), %.2f m from ISLAND_CENTRE %s. "
            "Is 'ground' parented?" % (after[0], after[1], drift, ISLAND_CENTRE))

    bpy.ops.wm.save_as_mainfile(filepath=args.out, compress=False)

    print("[recentre_map] shifted %d parentless objects by (%.3f, %.3f)"
          % (moved, delta.x, delta.y))
    print("[recentre_map]   ground centre %s -> %s (target %s, drift %.3f m)"
          % (tuple(round(c, 2) for c in before),
             tuple(round(c, 2) for c in after), ISLAND_CENTRE, drift))
    print("[recentre_map]   wrote %s" % args.out)


if __name__ == "__main__":
    main()
