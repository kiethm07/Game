"""Flatten the castle island so the fortress has level ground to stand on.

The island was authored with ~4 units of relief across its plateau (12 m at
WORLD_SCALE, stdev 1.10). Every structure on it was then seated against that
curve, which is why seating it has been so fragile: a wall run crosses a swell,
one end lifts, and bedding the terrain up under the high end only makes the low
end worse. Flat ground removes the problem rather than managing it.

Three rules, all learned the hard way on this mesh:

**Mask by slope and height, never by distance from the border.** `ground` is a
closed manifold with no boundary edges, so a border mask returns nothing, and
the island is small and deeply indented so a distance-from-waterline mask
starves exactly the walled compound it is meant to level. Slope says "plateau
or cliff" and height says "land or shore", and together they hold the compound
at full strength while protecting the cliffs that make it read as an island.

**Move the fortress as one rigid body.** Re-seating each piece by the gap under
its own footprint looks right per object and destroys the assembly -- walls,
towers, gates and the keep have authored relative heights, and doing this once
pulled `castle.001` and `castle tower.001`, the same building, 3.29 apart. So
every placed structure takes the *same* offset: the median gap over all of
them. Whatever is left is closed afterwards by tools/seat_art.py bedding the
terrain up, which is a local fix for a local residue.

**Leave the seams alone.** `mountain_path` meets the island at its northwest
shoulder and `ground_east` across the ravine; flattening right up to those
would open a step the player cannot climb. Vertices within FLATTEN_SEAM_GUARD
of another terrain mesh keep their height.

Usage:
    blender --background <level>.blend --python tools/flatten_island.py -- \
        [--out <path>] [--dry-run] [--strength 1.0]
"""

import argparse
import math
import statistics
import sys

import bpy
from mathutils import Vector

ISLAND = "ground"
OTHER_TERRAIN = ("mountain_path", "ground_east")

STRUCTURE_COLLECTIONS = ("finals buildings", "buildings")
PARKED_MAX_Z = -10.0
BASE_BAND = 0.35

WATER_LEVEL = -2.88

# Plateau where the surface is flat-ish and well above water; cliff and shore
# where it is not. Both are smoothstepped so the transition is a slope, not a
# terrace.
FLATTEN_SLOPE_MID = 0.55
FLATTEN_SLOPE_SOFT = 0.30
FLATTEN_HEIGHT_MID = WATER_LEVEL + 0.5
FLATTEN_HEIGHT_SOFT = 1.5

# How far a vertex must be from another terrain mesh to be flattened.
FLATTEN_SEAM_GUARD = 6.0

# Causeways need their deck level with the ground, not with the median.
#
# A bridge is the one structure whose *top* is the walking surface: its base is
# five units down in the channel, so the rigid fortress offset -- a median over
# 52 pieces standing on the plateau -- says nothing useful about it. Flattening
# the island to -0.21 left the deck at -0.45, a 0.72 m step down at the bridge
# mouth against a 0.5 m limit, and phase 2 lost everything past the near bank.
# These are lifted as one group until the deck meets the land at their ends.
DECK_GROUPS = (("bridge",),)
DECK_CLEARANCE = 0.02


def smoothstep(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3.0 - 2.0 * t)


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


def terrain_height(obj, x, y):
    inv = obj.matrix_world.inverted()
    hit, loc, _, _ = obj.ray_cast(
        inv @ Vector((x, y, 1000.0)),
        (inv.to_3x3() @ Vector((0.0, 0.0, -1.0))).normalized())
    return (obj.matrix_world @ loc).z if hit else None


def base_gaps(obj, island):
    pts = evaluated_points(obj)
    if not pts:
        return []
    floor = min(p.z for p in pts)
    gaps = []
    for p in pts:
        if p.z > floor + BASE_BAND:
            continue
        z = terrain_height(island, p.x, p.y)
        if z is not None:
            gaps.append(p.z - z)
    return gaps


def parse_args(argv):
    argv = argv[argv.index("--") + 1:] if "--" in argv else []
    parser = argparse.ArgumentParser(prog="flatten_island.py")
    parser.add_argument("--out", default=None)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--strength", type=float, default=1.0,
                        help="1.0 is dead flat; 0.5 halves the relief")
    return parser.parse_args(argv)


def main():
    args = parse_args(sys.argv)

    for name in ("landscape", "buildings", "rocks", "trees",
                 "finals buildings", "final rocks"):
        collection = bpy.data.collections.get(name)
        if collection is not None:
            collection.hide_viewport = False
    bpy.context.view_layer.update()

    island = bpy.data.objects.get(ISLAND)
    if island is None:
        raise SystemExit("no object named %r" % ISLAND)

    matrix = island.matrix_world
    inverse = matrix.inverted()
    normal_matrix = matrix.to_3x3().inverted().transposed()
    mesh = island.data

    # Seam guard: plan-view footprints of the neighbouring terrain meshes.
    seams = []
    for name in OTHER_TERRAIN:
        other = bpy.data.objects.get(name)
        if other is None:
            continue
        pts = [other.matrix_world @ v.co for v in other.data.vertices]
        seams.append((min(p.x for p in pts) - FLATTEN_SEAM_GUARD,
                      min(p.y for p in pts) - FLATTEN_SEAM_GUARD,
                      max(p.x for p in pts) + FLATTEN_SEAM_GUARD,
                      max(p.y for p in pts) + FLATTEN_SEAM_GUARD))

    def near_seam(x, y):
        return any(x0 <= x <= x1 and y0 <= y <= y1 for x0, y0, x1, y1 in seams)

    normals = mesh.vertex_normals
    world = [matrix @ v.co for v in mesh.vertices]
    masks = []
    for i, v in enumerate(mesh.vertices):
        nz = (normal_matrix @ Vector(normals[i].vector)).normalized().z
        w = world[i]
        mask = (smoothstep((nz - FLATTEN_SLOPE_MID) / FLATTEN_SLOPE_SOFT)
                * smoothstep((w.z - FLATTEN_HEIGHT_MID) / FLATTEN_HEIGHT_SOFT))
        if near_seam(w.x, w.y):
            mask = 0.0
        masks.append(mask)

    plateau = [world[i].z for i, m in enumerate(masks) if m > 0.5]
    if not plateau:
        raise SystemExit("no plateau found to flatten -- check the mask")
    target = statistics.median(plateau)

    before = statistics.pstdev(plateau)
    structures = placed_structures()
    gaps_before = {o.name: base_gaps(o, island) for o in structures}

    moved = 0
    if not args.dry_run:
        for i, v in enumerate(mesh.vertices):
            mask = masks[i] * args.strength
            if mask <= 0.0:
                continue
            w = world[i]
            new_z = w.z + (target - w.z) * mask
            if abs(new_z - w.z) < 1e-6:
                continue
            v.co = inverse @ Vector((w.x, w.y, new_z))
            moved += 1
        mesh.update()
        bpy.context.view_layer.update()

    after_pts = [(matrix @ mesh.vertices[i].co).z
                 for i, m in enumerate(masks) if m > 0.5]
    after = statistics.pstdev(after_pts)

    # One offset for the whole fortress.
    medians = []
    for obj in structures:
        gaps = base_gaps(obj, island)
        if gaps:
            medians.append(statistics.median(gaps))
    shift = -statistics.median(medians) if medians else 0.0
    if not args.dry_run and medians:
        for obj in structures:
            obj.location.z += shift
        bpy.context.view_layer.update()

    # Causeway decks, as groups.
    deck_report = []
    for prefixes in DECK_GROUPS:
        group = [o for o in structures
                 if any(o.name.startswith(p) for p in prefixes)]
        if not group:
            continue
        pts = [p for o in group for p in evaluated_points(o)]
        deck = max(p.z for p in pts)
        # Ground at the group's ends: sample just beyond its footprint.
        xs = [p.x for p in pts]
        ys = [p.y for p in pts]
        mid_y = (min(ys) + max(ys)) * 0.5
        ends = [terrain_height(island, min(xs) - 1.5, mid_y),
                terrain_height(island, max(xs) + 1.5, mid_y)]
        ends = [z for z in ends if z is not None]
        if not ends:
            continue
        land = max(ends)
        lift = (land - DECK_CLEARANCE) - deck
        if abs(lift) < 1e-3:
            continue
        if not args.dry_run:
            for o in group:
                o.location.z += lift
            bpy.context.view_layer.update()
        deck_report.append((prefixes[0], len(group), round(deck, 2),
                            round(land, 2), round(lift, 2)))

    residual = []
    for obj in structures:
        gaps = base_gaps(obj, island)
        if gaps:
            residual.append(min(gaps))

    print("[flatten_island] plateau %d verts, target z %.2f, strength %.2f"
          % (len(plateau), target, args.strength))
    print("[flatten_island]   relief stdev %.2f -> %.2f  (%d verts moved)"
          % (before, after, moved))
    print("[flatten_island]   fortress moved as one body by %+.2f "
          "(median gap over %d structures)" % (shift, len(medians)))
    for name, n, deck, land, lift in deck_report:
        print("[flatten_island]   %s deck (%d pieces) %+.2f -> land %+.2f, "
              "raised %+.2f" % (name, n, deck, land, lift))
    if residual:
        print("[flatten_island]   residual clearance min %+.2f median %+.2f "
              "max %+.2f; %d still airborne, for seat_art to bed"
              % (min(residual), statistics.median(residual), max(residual),
                 sum(1 for g in residual if g > 0.05)))

    if not args.dry_run:
        out = args.out or bpy.data.filepath
        bpy.ops.wm.save_as_mainfile(filepath=out, compress=False)
        print("[flatten_island] wrote %s" % out)


if __name__ == "__main__":
    main()
