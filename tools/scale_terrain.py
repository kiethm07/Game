"""Shrink `ground_east` and `battleground` to half their plan area.

Area goes as the square of a linear scale, so half the area is a factor of
1/sqrt(2) = 0.70711 in plan.

Three decisions worth stating, because each of them breaks something if made
the other way.

**Plan only, never Z.** Scaling height as well would preserve the terrain's
shape exactly, which sounds better and is not: the water plane, the ravine
seam with `ground`, and the height the mountain path lands at are all fixed
elsewhere, so compressing Z would move the shoreline and drop the pass's
landing away from the terrain that receives it. Slopes steepen by 1.414 as a
consequence, which the forest can afford (its walkable ground reads normals of
0.99-1.0) and the battleground does not notice at all (it is flat).

**Each pivot is a junction, not a centre.** Scaling a mesh about its own middle
pulls its edges inward, and both of these meshes are joined to something at an
edge. `ground_east` is anchored on its west edge, where the ravine seam with
`ground` is and where phase 1's west wall stands. `battleground` is anchored at
the point the mountain path lands on it, which is also phase 3's spawn -- so
the arena shrinks around the player's arrival rather than retreating from it.

**Mesh data, not object scale.** Scaling the object transform would scale its
hair particle instances too, and the forest would come out full of bonsai. The
vertices move; the object's scale is left alone.

`path1` crosses the ravine, so it cannot simply be scaled with the forest --
its western end lies on `ground`, which is not moving. It is warped instead:
full scale from the pivot eastward, fading to none over a band on the island
side, so the join stays smooth and only the stretch actually lying on
`ground_east` is resized.

Usage:
    blender --background <level>.blend --python tools/scale_terrain.py -- \
        [--out <path>] [--dry-run]
"""

import argparse
import json
import math
import sys

import bpy
from mathutils import Vector

FACTOR = math.sqrt(0.5)          # 0.70711 -> half the plan area

# name -> pivot in authored XY. See the module docstring for why these are the
# junctions rather than the centres.
SCALES = {
    "ground_east": (-111.45, -16.85),
    "battleground": (-240.2, 14.2),
}

# path1 is warped rather than scaled. Full effect at the pivot and eastward,
# fading to nothing this far west of it -- on `ground`, which does not move.
PATH_WARP = {"path1": ("ground_east", 12.0)}


def smoothstep(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3.0 - 2.0 * t)


def plan_area(obj):
    depsgraph = bpy.context.evaluated_depsgraph_get()
    evaluated = obj.evaluated_get(depsgraph)
    mesh = evaluated.to_mesh()
    if mesh is None:
        return 0.0
    try:
        mesh.calc_loop_triangles()
        matrix = obj.matrix_world
        normal_matrix = matrix.to_3x3().inverted().transposed()
        total = 0.0
        for tri in mesh.loop_triangles:
            if (normal_matrix @ tri.normal).normalized().z <= 0.1:
                continue
            a, b, c = (matrix @ mesh.vertices[i].co for i in tri.vertices)
            total += abs((b.x - a.x) * (c.y - a.y)
                         - (c.x - a.x) * (b.y - a.y)) * 0.5
        return total
    finally:
        evaluated.to_mesh_clear()


def mapped(point, pivot, factor=FACTOR):
    return (pivot[0] + factor * (point[0] - pivot[0]),
            pivot[1] + factor * (point[1] - pivot[1]))


def scale_mesh(obj, pivot):
    matrix = obj.matrix_world
    inverse = matrix.inverted()
    for vert in obj.data.vertices:
        world = matrix @ vert.co
        x, y = mapped((world.x, world.y), pivot)
        vert.co = inverse @ Vector((x, y, world.z))
    obj.data.update()


def warp_path(obj, pivot, band):
    """Scale eastward of the pivot, fading to none `band` units west of it."""
    matrix = obj.matrix_world
    inverse = matrix.inverted()
    moved = 0
    for spline in obj.data.splines:
        points = (spline.points if spline.type != "BEZIER"
                  else spline.bezier_points)
        for point in points:
            world = matrix @ Vector(point.co[:3])
            t = smoothstep((world.x - (pivot[0] - band)) / band)
            factor = 1.0 + t * (FACTOR - 1.0)
            x, y = mapped((world.x, world.y), pivot, factor)
            local = inverse @ Vector((x, y, world.z))
            if spline.type == "BEZIER":
                dx, dy = local.x - point.co.x, local.y - point.co.y
                point.co.x, point.co.y = local.x, local.y
                point.handle_left.x += dx
                point.handle_left.y += dy
                point.handle_right.x += dx
                point.handle_right.y += dy
            else:
                point.co = (local.x, local.y, local.z, point.co[3])
            moved += 1
    return moved


# Everything in the phase profiles that is an authored XY, so the constants can
# be regenerated rather than re-derived by hand -- 40 literals is 40 chances to
# fumble the arithmetic.
PROFILE_POINTS = {
    "phase1_forest": ("ground_east", {
        "PLAY_CENTRE": (-78.5, -16.9),
        "BOUNDARY_RECT_min": (-108.5, -55.0),
        "BOUNDARY_RECT_max": (-48.0, 22.0),
        "PLAYER_SPAWN_XY": (-52.0, -16.0),
        "PLAYER_FACES_XY": (-108.0, -16.0),
        "ENEMY_01": (-70.0, -8.0),
        "ENEMY_02": (-78.5, -16.9),
        "ENEMY_03": (-96.0, -22.0),
        "LM_spawn clearing": (-52.0, -16.0),
        "LM_mid forest": (-78.5, -16.9),
        "LM_north woods": (-78.0, 8.0),
        "LM_south woods": (-80.0, -40.0),
        "LM_ravine edge": (-106.0, -16.0),
    }),
    "phase3_battlefield": ("battleground", {
        "PLAY_CENTRE": (-249.3, 4.4),
        "BOUNDARY_RECT_min": (-299.0, -45.0),
        "BOUNDARY_RECT_max": (-200.0, 54.0),
        "PLAYER_SPAWN_XY": (-240.2, 14.2),
        "PLAYER_FACES_XY": (-270.0, 10.0),
        "ENEMY_01": (-260.0, 0.0),
        "ENEMY_02": (-270.0, 10.0),
        "ENEMY_03": (-280.0, 20.0),
        "LM_pass landing": (-240.2, 14.2),
        "LM_arena centre": (-249.3, 4.4),
        "LM_north field": (-270.0, 30.0),
        "LM_south field": (-262.0, -26.0),
        "LM_far end": (-292.0, 40.0),
    }),
}

PROFILE_RADII = {
    "phase1_forest": {"SCATTER_PLAY_RADIUS": 50.0, "PLAY_RADIUS": 45.0},
    "phase3_battlefield": {"SCATTER_PLAY_RADIUS": 70.0, "PLAY_RADIUS": 58.0},
}


def parse_args(argv):
    argv = argv[argv.index("--") + 1:] if "--" in argv else []
    parser = argparse.ArgumentParser(prog="scale_terrain.py")
    parser.add_argument("--out", default=None)
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args(argv)


def main():
    args = parse_args(sys.argv)

    for name in ("landscape", "buildings", "rocks", "trees",
                 "finals buildings", "final rocks"):
        collection = bpy.data.collections.get(name)
        if collection is not None:
            collection.hide_viewport = False
    bpy.context.view_layer.update()

    before = {}
    for name in SCALES:
        before[name] = plan_area(bpy.data.objects[name])

    if not args.dry_run:
        for name, pivot in SCALES.items():
            scale_mesh(bpy.data.objects[name], pivot)
        for path_name, (terrain, band) in PATH_WARP.items():
            obj = bpy.data.objects.get(path_name)
            if obj is not None:
                warp_path(obj, SCALES[terrain], band)
        bpy.context.view_layer.update()

    print("[scale_terrain] factor %.5f (half the plan area)" % FACTOR)
    for name in SCALES:
        after = plan_area(bpy.data.objects[name])
        obj = bpy.data.objects[name]
        cs = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
        print("    %-13s area %8.1f -> %8.1f  (x %.1f..%.1f, y %.1f..%.1f)"
              % (name, before[name], after,
                 min(c.x for c in cs), max(c.x for c in cs),
                 min(c.y for c in cs), max(c.y for c in cs)))

    print("[scale_terrain] transformed profile constants:")
    for profile, (terrain, points) in PROFILE_POINTS.items():
        pivot = SCALES[terrain]
        print("  %s (pivot %s)" % (profile, pivot))
        for key, value in points.items():
            x, y = mapped(value, pivot)
            print("      %-22s (%.2f, %.2f)" % (key, x, y))
        for key, value in PROFILE_RADII[profile].items():
            print("      %-22s %.1f" % (key, value * FACTOR))

    if not args.dry_run:
        out = args.out or bpy.data.filepath
        bpy.ops.wm.save_as_mainfile(filepath=out, compress=False)
        print("[scale_terrain] wrote %s" % out)


if __name__ == "__main__":
    main()
