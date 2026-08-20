"""Sit the paths and the fortress down onto the terrain they stand on.

Two unrelated causes of the same symptom, fixed separately because the right
answer differs.

PATHS. `path1` and `path2` are curves, and a Blender curve's `extrude` grows
the profile *symmetrically* about the spline: path1's extrude of 1.0 makes a
slab two units thick whose top rides a full unit -- 3 m at WORLD_SCALE -- above
the ground the spline was drawn on. It reads as a road on stilts. The fix is to
make the ribbon thin and then drop it so its top rides just proud of the
terrain, which is what a path should do.

STRUCTURES. The fortress is not offset as a whole -- the median gap under a
base is -0.03 units -- but 8 of the 52 pieces clear the ground entirely, by up
to 0.22 at their tightest point and 0.6 at their loosest. Lowering each piece
by the gap under it would close every one of those and wreck the assembly:
walls, towers, gates and the keep have authored relative heights, and moving
them independently is what once pulled `castle.001` and `castle tower.001` --
the same building -- 3.29 units apart vertically. So the buildings are not
moved at all. The terrain is brought up to meet them instead, under the true
base geometry, with a falloff so the raise reads as a foundation rather than a
step, and a cap so a piece overhanging a shoreline cannot build a spike.

Usage:
    blender --background <level>.blend --python tools/seat_art.py -- \
        [--out <path>] [--dry-run]
"""

import argparse
import math
import os
import statistics
import sys

import bpy
from mathutils import Vector

# Structures live in these; `buildings` is mixed and filtered by height, the
# same rule make_castle_level.py uses.
STRUCTURE_COLLECTIONS = ("finals buildings", "buildings")
PARKED_MAX_Z = -10.0

TERRAIN = ("ground", "ground_east")
PATHS = ("path1", "path2")

# --- Paths ------------------------------------------------------------------

# Half-thickness of the ribbon after the fix. The visible slab is twice this,
# and its top rides PATH_EXTRUDE above the spline, so this is what decides how
# much of a raised trail the path reads as.
#
# 1.0 as authored was a road on stilts. 0.08 was the opposite mistake -- a
# 0.24 m lip at WORLD_SCALE, which disappears into the grass. 0.25 puts the
# tread about 0.75 m proud: clearly a path, not a viaduct.
PATH_EXTRUDE = 0.25

# path1 also carries a Solidify; trimmed to match rather than removed, so the
# ribbon keeps a rim and does not read as a zero-thickness decal.
PATH_SOLIDIFY = 0.03

# How far above the terrain the spline itself is set. Small and positive: the
# curve only approximates its control points, so it sags between them, and
# without a little lift the tread dips under the grass in the hollows.
PATH_RIDE = 0.06

# Control points are inserted before projecting. path1's were 4.21 units apart,
# far coarser than the ground undulates, so dropping them onto the terrain
# still left the curve sagging up to 0.3 between them. Bringing them to ~1.4
# took the sag with it. Shape-preserving: subdividing a NURBS inserts knots, it
# does not move the curve.
#
# Expressed as a target spacing rather than a number of cuts so this script can
# be re-run. It has to be: the terrain is edited after this in some passes (see
# tools/scale_terrain.py) and the paths must then be re-projected, at which
# point a fixed cut count would subdivide an already-dense curve again -- 103
# points to 307, and again on the next run.
PATH_TARGET_SPACING = 2.0
PATH_MAX_CUTS = 4

# --- Structures -------------------------------------------------------------

# Vertices within this of an object's lowest point count as its base.
BASE_BAND = 0.35

# Terrain is brought to just under the base, not exactly to it, so a raised
# vertex cannot poke through a floor.
BED_EMBED = 0.05

# Full strength within CORE of a base point, fading to nothing at RADIUS.
BED_CORE = 0.6
BED_RADIUS = 1.6

# No terrain vertex is raised further than this, whatever the gap says. Without
# it a gate whose footprint overhangs the shore drags the shoreline up with it.
BED_CAP = 1.0

# Only terrain within this vertical distance of the base is a candidate. The
# island is a closed shell, and the distance test is in plan view, so without
# this the underside 8 units below a wall is "near" it too.
BED_Z_WINDOW = 3.0


def placed_structures():
    out = []
    for name in STRUCTURE_COLLECTIONS:
        collection = bpy.data.collections.get(name)
        if collection is None:
            continue
        for obj in collection.objects:
            if obj.type != "MESH":
                continue
            top = max((obj.matrix_world @ Vector(c)).z for c in obj.bound_box)
            if top > PARKED_MAX_Z:
                out.append(obj)
    return sorted(out, key=lambda o: o.name)


def evaluated_points(obj):
    depsgraph = bpy.context.evaluated_depsgraph_get()
    evaluated = obj.evaluated_get(depsgraph)
    mesh = evaluated.to_mesh()
    if mesh is None:
        return []
    try:
        return [obj.matrix_world @ v.co for v in mesh.vertices]
    finally:
        evaluated.to_mesh_clear()


def terrain_objects():
    return [bpy.data.objects[n] for n in TERRAIN if n in bpy.data.objects]


def terrain_height(obj, x, y):
    inv = obj.matrix_world.inverted()
    hit, loc, _, _ = obj.ray_cast(
        inv @ Vector((x, y, 1000.0)),
        (inv.to_3x3() @ Vector((0.0, 0.0, -1.0))).normalized())
    return (obj.matrix_world @ loc).z if hit else None


def terrain_height_any(objs, x, y):
    zs = [z for z in (terrain_height(o, x, y) for o in objs) if z is not None]
    return max(zs) if zs else None


# --- Buried structures ------------------------------------------------------

# A structure whose base sits at least this far under the terrain is not merely
# bedded in, it is sunk. `warehouse.001` was at -1.07 -- a 4.38-tall house with
# a quarter of it underground and its doorway arch half-swallowed.
BURIED_LIMIT = -0.5

# ...but only lift one if nothing is attached to it. Raising a piece by the gap
# under its own footprint is exactly the move that once pulled `castle.001` and
# `castle tower.001` 3.29 apart, and a wall run is a chain of pieces with
# authored relative heights. A structure whose nearest neighbour is further
# than this is standing on its own and can be moved without dragging an
# assembly out of alignment. Measured: the buried house is 4.84 from anything
# else, while every buried wall is 1.5-3.0 from its neighbours and the bridge
# spans are 3.35 apart -- so this separates them with room to spare, and the
# bridges (buried 4.6 over their channel, and correctly so) are never touched.
ISOLATION_MIN = 4.0

# Lift to just-embedded rather than flush, and let bed_terrain close whatever
# gap is left at the shallow end.
BURIED_TARGET = -0.05


# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

def seat_paths(terrains, dry_run):
    report = []
    for name in PATHS:
        obj = bpy.data.objects.get(name)
        if obj is None or obj.type != "CURVE":
            continue
        before_extrude = obj.data.extrude
        before_gap = path_gaps(obj, terrains)

        moved = 0
        if not dry_run:
            obj.data.extrude = PATH_EXTRUDE
            for mod in obj.modifiers:
                if mod.type == "SOLIDIFY":
                    mod.thickness = PATH_SOLIDIFY

            # Thinning alone is not enough. The spline keeps whatever height it
            # was drawn at, so a path that was authored floating in one stretch
            # and sunk in another simply becomes a thin ribbon doing both. Each
            # control point is dropped onto the terrain under it instead, which
            # is what makes it a path rather than a ribbon that happens to be
            # nearby. NURBS only approximates its control points, so the result
            # is a smoothed following of the ground rather than a rigid drape --
            # which is what a trail should do anyway.
            subdivide_curve(obj, cuts_needed(obj))
            moved = project_to_terrain(obj, terrains)
            bpy.context.view_layer.update()

        after_gap = path_gaps(obj, terrains)
        report.append({
            "name": name,
            "extrude": (round(before_extrude, 3), round(obj.data.extrude, 3)),
            "moved": moved,
            "gap_before": summarise(before_gap),
            "gap_after": summarise(after_gap),
        })
    return report


def cuts_needed(obj):
    """How many cuts per segment bring control points to the target spacing."""
    points = [obj.matrix_world @ Vector(p.co[:3])
              for s in obj.data.splines
              for p in (s.points if s.type != "BEZIER" else s.bezier_points)]
    if len(points) < 2:
        return 0
    length = sum((points[i + 1] - points[i]).length
                 for i in range(len(points) - 1))
    spacing = length / (len(points) - 1)
    if spacing <= PATH_TARGET_SPACING:
        return 0
    return min(PATH_MAX_CUTS,
               int(math.ceil(spacing / PATH_TARGET_SPACING)) - 1)


def subdivide_curve(obj, cuts):
    """Insert `cuts` control points into every segment, shape unchanged."""
    if cuts <= 0:
        return
    view_layer = bpy.context.view_layer
    previous = view_layer.objects.active
    for other in bpy.context.selected_objects:
        other.select_set(False)
    view_layer.objects.active = obj
    obj.select_set(True)
    try:
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.curve.select_all(action="SELECT")
        bpy.ops.curve.subdivide(number_cuts=cuts)
        bpy.ops.object.mode_set(mode="OBJECT")
    except RuntimeError as err:
        # Not fatal: without the extra points the path still gets projected,
        # it just tracks the ground more loosely. Worth saying so rather than
        # producing a quietly worse path.
        print("[seat_art] WARNING could not subdivide %s (%s); projecting the "
              "existing %d points instead" % (obj.name, err, cuts))
        if obj.mode != "OBJECT":
            bpy.ops.object.mode_set(mode="OBJECT")
    finally:
        obj.select_set(False)
        view_layer.objects.active = previous


def project_to_terrain(obj, terrains):
    """Set each control point's height to the terrain beneath it."""
    inverse = obj.matrix_world.inverted()
    moved = 0
    for spline in obj.data.splines:
        points = spline.points if spline.type != "BEZIER" else spline.bezier_points
        for point in points:
            world = obj.matrix_world @ Vector(point.co[:3])
            z = terrain_height_any(terrains, world.x, world.y)
            if z is None:
                continue
            local = inverse @ Vector((world.x, world.y, z + PATH_RIDE))
            if spline.type == "BEZIER":
                delta = local.z - point.co.z
                point.co.z = local.z
                point.handle_left.z += delta
                point.handle_right.z += delta
            else:
                # A NURBS point's co is 4D; the weight must survive.
                point.co = (local.x, local.y, local.z, point.co[3])
            moved += 1
    return moved


def path_gaps(obj, terrains):
    gaps = []
    for p in evaluated_points(obj):
        z = terrain_height_any(terrains, p.x, p.y)
        if z is not None:
            gaps.append(p.z - z)
    return gaps


def summarise(values):
    if not values:
        return None
    return {"min": round(min(values), 2),
            "median": round(statistics.median(values), 2),
            "max": round(max(values), 2)}


# ---------------------------------------------------------------------------
# Structures
# ---------------------------------------------------------------------------

def base_points(structures):
    """World-space vertices that ought to be touching ground."""
    points = []
    for obj in structures:
        pts = evaluated_points(obj)
        if not pts:
            continue
        floor = min(p.z for p in pts)
        points.extend(p for p in pts if p.z <= floor + BASE_BAND)
    return points


def lift_buried_structures(structures, terrains, dry_run):
    """Raise isolated structures that are sunk into the ground.

    Deliberately narrow. The rule elsewhere in this script is that buildings do
    not move and the terrain comes up to meet them, because the fortress is an
    assembly. That rule holds for anything joined to anything else; it does not
    help a standalone house sunk a metre into the hillside, where bedding the
    terrain up would only bury it further. So a piece is lifted only when it is
    both badly sunk and demonstrably on its own -- see ISOLATION_MIN.
    """
    centres = {}
    for obj in structures:
        pts = evaluated_points(obj)
        if pts:
            centres[obj.name] = Vector((
                (min(p.x for p in pts) + max(p.x for p in pts)) * 0.5,
                (min(p.y for p in pts) + max(p.y for p in pts)) * 0.5, 0.0))

    moved = []
    for obj in structures:
        pts = evaluated_points(obj)
        if not pts:
            continue
        floor = min(p.z for p in pts)
        gaps = []
        for p in pts:
            if p.z > floor + BASE_BAND:
                continue
            z = terrain_height_any(terrains, p.x, p.y)
            if z is not None:
                gaps.append(p.z - z)
        if not gaps:
            continue
        median = statistics.median(gaps)
        if median >= BURIED_LIMIT:
            continue

        neighbour = min(((centres[obj.name] - c).length, n)
                        for n, c in centres.items() if n != obj.name)
        if neighbour[0] <= ISOLATION_MIN:
            print("[seat_art] left buried %-20s median %+.2f -- %s is only "
                  "%.2f away, moving it would break the run"
                  % (obj.name, median, neighbour[1], neighbour[0]))
            continue

        lift = BURIED_TARGET - median
        if not dry_run:
            obj.location.z += lift
        moved.append((obj.name, median, lift, neighbour[0]))

    if not dry_run and moved:
        bpy.context.view_layer.update()
    return moved


def bed_terrain(structures, terrains, dry_run):
    """Raise terrain under the structures' base geometry."""
    required = []
    for p in base_points(structures):
        z = terrain_height_any(terrains, p.x, p.y)
        if z is None or p.z - BED_EMBED <= z:
            continue                     # already seated or buried
        required.append((p.x, p.y, p.z - BED_EMBED))

    grid = {}
    for entry in required:
        key = (int(entry[0] // BED_RADIUS), int(entry[1] // BED_RADIUS))
        grid.setdefault(key, []).append(entry)

    stats = {"gap_points": len(required), "moved": 0, "max_raise": 0.0,
             "capped": 0}
    if dry_run or not required:
        return stats

    for terrain in terrains:
        matrix = terrain.matrix_world
        inverse = matrix.inverted()
        mesh = terrain.data
        for vert in mesh.vertices:
            world = matrix @ vert.co
            gi, gj = int(world.x // BED_RADIUS), int(world.y // BED_RADIUS)
            best = None
            for di in (-1, 0, 1):
                for dj in (-1, 0, 1):
                    for (px, py, pz) in grid.get((gi + di, gj + dj), ()):
                        if abs(pz - world.z) > BED_Z_WINDOW:
                            continue
                        d = math.hypot(px - world.x, py - world.y)
                        if d >= BED_RADIUS:
                            continue
                        if d <= BED_CORE:
                            w = 1.0
                        else:
                            t = (BED_RADIUS - d) / (BED_RADIUS - BED_CORE)
                            w = t * t * (3.0 - 2.0 * t)
                        target = world.z + w * (pz - world.z)
                        if best is None or target > best:
                            best = target
            if best is None or best <= world.z:
                continue
            raise_by = best - world.z
            if raise_by > BED_CAP:
                raise_by = BED_CAP
                stats["capped"] += 1
            stats["max_raise"] = max(stats["max_raise"], raise_by)
            stats["moved"] += 1
            vert.co = inverse @ Vector((world.x, world.y, world.z + raise_by))
        mesh.update()

    bpy.context.view_layer.update()
    stats["max_raise"] = round(stats["max_raise"], 3)
    return stats


def structure_gaps(structures, terrains):
    """Per-object (min, median) gap at the base, and how many float."""
    mins = []
    for obj in structures:
        pts = evaluated_points(obj)
        if not pts:
            continue
        floor = min(p.z for p in pts)
        gaps = []
        for p in pts:
            if p.z > floor + BASE_BAND:
                continue
            z = terrain_height_any(terrains, p.x, p.y)
            if z is not None:
                gaps.append(p.z - z)
        if gaps:
            mins.append(min(gaps))
    return mins


def parse_args(argv):
    argv = argv[argv.index("--") + 1:] if "--" in argv else []
    parser = argparse.ArgumentParser(prog="seat_art.py")
    parser.add_argument("--out", default=None,
                        help="where to save (defaults to the input .blend)")
    parser.add_argument("--dry-run", action="store_true",
                        help="measure and report, change nothing")
    return parser.parse_args(argv)


def main():
    args = parse_args(sys.argv)

    # Nothing below reads the depsgraph until the art is visible: a hidden
    # collection is not evaluated, and every measurement here is a raycast.
    for name in ("landscape", "buildings", "rocks", "trees",
                 "finals buildings", "final rocks"):
        collection = bpy.data.collections.get(name)
        if collection is not None:
            collection.hide_viewport = False
    bpy.context.view_layer.update()

    terrains = terrain_objects()
    if not terrains:
        raise SystemExit("no terrain objects (%s) -- wrong .blend?"
                         % ", ".join(TERRAIN))
    structures = placed_structures()

    before = structure_gaps(structures, terrains)
    paths = seat_paths(terrains, args.dry_run)
    lifted = lift_buried_structures(structures, terrains, args.dry_run)
    bed = bed_terrain(structures, terrains, args.dry_run)
    after = structure_gaps(structures, terrains)

    print("[seat_art] paths")
    for row in paths:
        print("    %-6s extrude %s -> %s, %d control points dropped onto the "
              "terrain" % (row["name"], row["extrude"][0], row["extrude"][1],
                           row["moved"]))
        print("           gap before %s" % (row["gap_before"],))
        print("           gap after  %s" % (row["gap_after"],))

    for name, median, lift, near in lifted:
        print("[seat_art] lifted %-20s out of the ground by %.2f "
              "(was %+.2f under; nearest structure %.2f away)"
              % (name, lift, median, near))
    print("[seat_art] structures: %d placed, %d base points sat above terrain"
          % (len(structures), bed["gap_points"]))
    print("           terrain vertices raised %d (max %.2f, %d hit the cap)"
          % (bed["moved"], bed["max_raise"], bed["capped"]))
    print("           airborne pieces %d -> %d"
          % (sum(1 for g in before if g > 0.05),
             sum(1 for g in after if g > 0.05)))
    print("           worst clearance %+.2f -> %+.2f"
          % (max(before), max(after)))

    if not args.dry_run:
        out = args.out or bpy.data.filepath
        bpy.ops.wm.save_as_mainfile(filepath=out, compress=False)
        print("[seat_art] wrote %s" % out)


if __name__ == "__main__":
    main()
