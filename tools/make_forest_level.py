"""Turn the imported Sketchfab forest diorama into a playable first level.

Unlike make_greybox_level.py, which builds its map out of nothing, this script
takes a piece of bought/downloaded art as its input and does the two jobs that
stand between an imported model and something export_level.py will accept:

  1. Scales it. The import arrives as a diorama, not a place -- its cabin is
     0.167 Blender units from ground to roof ridge.
  2. Builds collision for it. The art is a smooth triangle mesh; the engine's
     world is axis-aligned boxes and rectangular ramps (PhysicsObstacle). The
     terrain therefore has to be *approximated* by proxies rather than used
     directly, which is what most of this file is about.

Usage:
    blender --background source/levels/forest.blend \
        --python tools/make_forest_level.py -- [--out source/levels/forest.blend]

Then export it, exactly as for any other level:
    blender --background source/levels/forest.blend \
        --python tools/export_level.py -- --out-dir assets/levels/forest
    python3 tools/verify_level.py assets/levels/forest

Re-running is safe. The script writes its own output back over its own input, so
everything it does is expressed as "set to", never "multiply by": the scale is
restored from a custom property stashed on the model root the first time round,
and COLLISION/MARKERS are deleted and rebuilt from scratch on every run. That
matters because the .blend is the tracked source of truth -- there is no pristine
copy of the import to fall back to if a second run doubled the scale.
"""

import argparse
import math
import os
import sys

import bpy
from mathutils import Vector

# Blender does not put a --python script's own directory on sys.path.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from level_terrain import (          # noqa: E402
    add_box, add_marker, clear_collection, game_yaw, get_collection,
    measure_error, merge_rectangles, move_to_collection, quantize_and_relax,
    report_steps, sample_ground_grid, terrain_height,
)

# --- Scale ------------------------------------------------------------------

# Three independent features of the cabin agree on this number, which is why it
# is 26 and not a round 25 or 30:
#
#   porch steps      0.007 u rise  ->  0.18 m, a standard stair riser
#   deck to eave     0.085 u       ->  2.21 m, headroom you can walk under
#   ground to ridge  0.168 u       ->  4.36 m, a one-room cabin
#
# Against those, Player::BODY_HEIGHT (1.8 m) stands just below the porch eave
# and a shade over the paddock fence, and the big conifers come out at 13.7 m.
SCALE = 26.0

# Set on the model root the first time the scale is changed, so a re-run scales
# the original import rather than the already-scaled result.
IMPORT_SCALE_KEY = "forest_import_scale"

# --- Collections ------------------------------------------------------------

VISUAL = "VISUAL"
COLLISION = "COLLISION"
MARKERS = "MARKERS"

# Not read by export_level.py. Somewhere to park geometry that is neither art
# nor collision, so it stops rendering without being deleted.
REFERENCE = "REFERENCE"

# The asset ships a coarse 216-face shell named "Physic" that hovers ~0.3 m over
# the real terrain -- the original author's collision mesh. It is invisible in
# intent but not in fact, so it goes to REFERENCE rather than into the export.
REFERENCE_OBJECTS = ("Physic_54",)

# --- Names of things in the import ------------------------------------------

MODEL_ROOT = "Sketchfab_model"
TERRAIN = "Object_56"      # the ground disc, child of Floor_26
HOUSE_MESH = "Object_58"   # house.scene: cabin, fences, rocks, path furniture
TREE_PREFIXES = ("big.tree", "medium.tree", "tree.", "house.tree")

# --- Collision layout, all in metres and all post-scale ----------------------

# How far out the player can walk. The island is a dome: flat to about 38 m,
# then falling away to a rim at 76 m, with the lake basin sunk into the slope.
# 38 is where that fall-off starts, and it is a cliff in the error rather than a
# gentle trend -- taking the play area out to 42 m triples the worst-case gap
# between the proxies and the ground they stand in for (0.43 m -> 1.04 m),
# because past that point the proxy surface is bridging the lake instead of
# following the meadow. The lake and the outer slope stay scenery.
PLAY_RADIUS = 38.0

# Ground proxy grid. Cells are height-limited and then merged into maximal
# rectangles, so this is the resolution of the approximation, not the proxy
# count -- flat ground collapses into a handful of big boxes.
GROUND_CELL = 4.5
GROUND_SAMPLES = 3          # NxN raycasts per cell, averaged
GROUND_THICKNESS = 3.0      # deep enough that nothing sub-steps through it

MAX_STEP = 0.3              # PhysicsManager.cpp:176

# The proxy surface is not the terrain -- it is the closest staircase to the
# terrain that the character can actually walk up.
#
# PhysicsManager treats a proxy top up to MAX_STEP above the feet as a floor to
# step onto and anything higher as a wall, so every neighbouring pair of ground
# boxes has to stay inside that budget. The art does not: this is stylised
# low-poly terrain whose facets are ~7 m across, with honest 0.5 m bumps in the
# middle of the "flat" plateau. Sampling it straight and rounding gave 136
# neighbour pairs over MAX_STEP -- 136 knee-high invisible walls in the arena.
#
# So the sampled heights are relaxed until no neighbour pair differs by more
# than STEP_LIMIT. Where the terrain is flat the relaxation changes nothing and
# the proxies sit on it exactly; where it bumps, the bump is spread across
# neighbouring cells instead of becoming a wall. build_ground reports how far
# the result ends up from the real ground, which is the price being paid.
#
# The whole thing runs in whole multiples of HEIGHT_QUANTUM rather than in
# metres. Heights have to be quantised anyway -- it is what lets equal cells
# merge into big rectangles instead of staying 200 individual boxes -- and
# quantising *after* a relaxation in metres can push a pair that just satisfied
# the limit back over it by up to one quantum. Relaxing in the quantised domain
# makes every difference an exact integer, so the constraint is enforced
# exactly instead of nearly.
HEIGHT_QUANTUM = 0.05
STEP_QUANTA = 5                              # 5 * 0.05 = 0.25 m, under MAX_STEP
STEP_LIMIT = STEP_QUANTA * HEIGHT_QUANTUM
RELAX_ITERATIONS = 400

# Boundary ring: a closed polygon of yawed boxes standing on the ground proxies,
# just inside their outer edge.
BOUNDARY_RADIUS = 35.0
BOUNDARY_SEGMENTS = 28
BOUNDARY_HEIGHT = 6.0
BOUNDARY_THICKNESS = 2.0

# Trees are collided as their trunk, measured off the bottom of each mesh. The
# canopy is deliberately not included: a big conifer's skirt is 8 m across, and
# wrapping that would turn a walkable forest into a maze of invisible cylinders.
TRUNK_SLICE = 1.5           # height band above the base the trunk is measured in
TRUNK_MIN_HALF = 0.45       # so the thinnest saplings are still something you hit
TRUNK_MAX_HALF = 1.6

# --- Spawns, in metres, as Blender XY ---------------------------------------

# The player arrives on the path east of the cabin, looking at it.
PLAYER_SPAWN_XY = (28.0, 3.0)
PLAYER_FACES_XY = (0.0, 2.0)

# (suffix, x, y) -- three Swordmen spread around the clearing.
ENEMY_SPAWNS_XY = [
    ("01", -7.0, 13.0),
    ("02", -20.0, -10.0),
    ("03", 11.0, -19.0),
]

# Viewport display colours, which is what the in-game debug overlay draws each
# proxy in. Colour-coded by role so the overlay is readable.
COLOR_GROUND = (0.32, 0.42, 0.26, 1.0)
COLOR_TREE = (0.42, 0.26, 0.14, 1.0)
COLOR_HOUSE = (0.55, 0.55, 0.58, 1.0)
COLOR_BOUNDARY = (0.70, 0.13, 0.13, 1.0)


# ---------------------------------------------------------------------------
# Blender helpers
# ---------------------------------------------------------------------------


# ---------------------------------------------------------------------------
# Step 1: scale
# ---------------------------------------------------------------------------

def apply_scale():
    """Scale the whole import, and flatten the delta transforms while at it.

    The Sketchfab importer parks the Y-up-to-Z-up correction in the root's
    *delta* rotation and lets the glTF node below cancel it. The net is
    identity, but it is a transform the glTF exporter and export_level.py's
    to_game have to agree about for no reason, so it is folded into the ordinary
    rotation here and the deltas are zeroed.
    """
    root = bpy.data.objects.get(MODEL_ROOT)
    if root is None:
        raise SystemExit("no %r object -- is this the Sketchfab forest import?"
                         % MODEL_ROOT)

    if IMPORT_SCALE_KEY not in root:
        root[IMPORT_SCALE_KEY] = list(root.scale)
    original = Vector(root[IMPORT_SCALE_KEY])

    world = root.matrix_world.copy()
    root.delta_location = (0.0, 0.0, 0.0)
    root.delta_rotation_euler = (0.0, 0.0, 0.0)
    root.delta_scale = (1.0, 1.0, 1.0)

    root.location = world.translation
    root.rotation_mode = "XYZ"
    root.rotation_euler = world.to_euler("XYZ")
    root.scale = original * SCALE

    bpy.context.view_layer.update()
    return root, original


# ---------------------------------------------------------------------------
# Step 3: proxies
# ---------------------------------------------------------------------------


def build_ground(collision, terrain):
    heights, n, origin = sample_ground_grid(
        terrain, PLAY_RADIUS, GROUND_CELL, GROUND_SAMPLES)
    converged = quantize_and_relax(
        heights, n, HEIGHT_QUANTUM, STEP_QUANTA, RELAX_ITERATIONS)
    error = measure_error(terrain, heights, n, origin, GROUND_CELL)
    rects = merge_rectangles(heights, n)

    for index, (i, j, w, h, z) in enumerate(rects):
        x0 = origin[0] + i * GROUND_CELL
        y0 = origin[1] + j * GROUND_CELL
        size_x = w * GROUND_CELL
        size_y = h * GROUND_CELL
        add_box(collision, "BOX_Ground_%03d" % index,
                centre=(x0 + size_x * 0.5, y0 + size_y * 0.5,
                        z - GROUND_THICKNESS * 0.5),
                size=(size_x, size_y, GROUND_THICKNESS),
                yaw_deg=0.0, colour=COLOR_GROUND)

    worst, over, _ = report_steps(heights, n, MAX_STEP)
    cells = sum(1 for row in heights for v in row if v is not None)
    return {"cells": cells, "boxes": len(rects), "converged": converged,
            "worst_step": worst, "steps_over_max": over, "error": error}


def build_trees(collision, terrain):
    count = 0
    skipped = 0
    for empty in sorted(bpy.data.objects, key=lambda o: o.name):
        if empty.type != "EMPTY" or not empty.name.startswith(TREE_PREFIXES):
            continue
        meshes = [c for c in empty.children if c.type == "MESH"]
        if not meshes:
            continue

        mesh = meshes[0]
        points = [mesh.matrix_world @ v.co for v in mesh.data.vertices]
        z_min = min(p.z for p in points)
        z_max = max(p.z for p in points)

        trunk = [p for p in points if p.z <= z_min + TRUNK_SLICE] or points
        min_x, max_x = min(p.x for p in trunk), max(p.x for p in trunk)
        min_y, max_y = min(p.y for p in trunk), max(p.y for p in trunk)
        cx, cy = (min_x + max_x) * 0.5, (min_y + max_y) * 0.5

        if math.hypot(cx, cy) > PLAY_RADIUS:
            skipped += 1
            continue

        half_x = min(max((max_x - min_x) * 0.5, TRUNK_MIN_HALF), TRUNK_MAX_HALF)
        half_y = min(max((max_y - min_y) * 0.5, TRUNK_MIN_HALF), TRUNK_MAX_HALF)

        # Sunk a metre below the base so the trunk still blocks where the tree
        # stands on ground the proxy grid quantised slightly downward.
        bottom = z_min - 1.0
        add_box(collision, "BOX_Tree_%s" % empty.name.replace(".", "_"),
                centre=(cx, cy, (bottom + z_max) * 0.5),
                size=(half_x * 2.0, half_y * 2.0, z_max - bottom),
                yaw_deg=0.0, colour=COLOR_TREE)
        count += 1

    return {"trees": count, "outside_play_area": skipped}


def build_house(collision, terrain):
    """One box round the cabin's walls.

    Measured rather than hardcoded: the walls are whatever house.scene geometry
    stands in a band above the porch deck near the middle of the map, which
    excludes the fences, rocks and flowerbeds sharing that mesh.
    """
    house = bpy.data.objects.get(HOUSE_MESH)
    if house is None:
        return {"house": 0}

    ground = terrain_height(terrain, 0.0, 2.0) or 0.0
    band = [house.matrix_world @ v.co for v in house.data.vertices]
    walls = [p for p in band
             if math.hypot(p.x, p.y - 2.0) <= 8.0
             and ground + 1.2 <= p.z <= ground + 3.0]
    if not walls:
        return {"house": 0}

    min_x, max_x = min(p.x for p in walls), max(p.x for p in walls)
    min_y, max_y = min(p.y for p in walls), max(p.y for p in walls)
    top = max(p.z for p in band
              if min_x <= p.x <= max_x and min_y <= p.y <= max_y)
    bottom = ground - 1.0

    add_box(collision, "BOX_House",
            centre=((min_x + max_x) * 0.5, (min_y + max_y) * 0.5,
                    (bottom + top) * 0.5),
            size=(max_x - min_x, max_y - min_y, top - bottom),
            yaw_deg=0.0, colour=COLOR_HOUSE)
    return {"house": 1,
            "footprint": (round(max_x - min_x, 2), round(max_y - min_y, 2)),
            "height": round(top - ground, 2)}


def build_boundary(collision, terrain):
    """A closed ring of yawed boxes at the edge of the ground proxies.

    Without it the player walks off the last ground box and falls: the island's
    outer slope has no collision at all, by design.
    """
    for index in range(BOUNDARY_SEGMENTS):
        angle = math.tau * index / BOUNDARY_SEGMENTS
        cx = math.cos(angle) * BOUNDARY_RADIUS
        cy = math.sin(angle) * BOUNDARY_RADIUS
        ground = terrain_height(terrain, cx, cy)
        if ground is None:
            ground = 0.0
        # Overlapped by 15% so the corners between segments cannot open a gap.
        span = math.tau * BOUNDARY_RADIUS / BOUNDARY_SEGMENTS * 1.15
        add_box(collision, "BOX_Boundary_%02d" % index,
                centre=(cx, cy, ground + BOUNDARY_HEIGHT * 0.5 - 1.0),
                size=(BOUNDARY_THICKNESS, span, BOUNDARY_HEIGHT),
                yaw_deg=math.degrees(angle),
                colour=COLOR_BOUNDARY)
    return {"boundary": BOUNDARY_SEGMENTS}


# ---------------------------------------------------------------------------
# Step 4: markers
# ---------------------------------------------------------------------------


def build_markers(markers, terrain):
    x, y = PLAYER_SPAWN_XY
    z = terrain_height(terrain, x, y)
    if z is None:
        raise SystemExit("player spawn (%.1f, %.1f) is off the terrain" % (x, y))
    add_marker(markers, "PLAYER_SPAWN", (x, y, z),
               game_yaw(PLAYER_SPAWN_XY, PLAYER_FACES_XY))

    placed = []
    for suffix, ex, ey in ENEMY_SPAWNS_XY:
        ez = terrain_height(terrain, ex, ey)
        if ez is None:
            raise SystemExit("enemy spawn (%.1f, %.1f) is off the terrain"
                             % (ex, ey))
        add_marker(markers, "ENEMY_Swordman_%s" % suffix, (ex, ey, ez),
                   game_yaw((ex, ey), PLAYER_SPAWN_XY))
        placed.append((suffix, round(ez, 2)))
    return {"player": (x, y, round(z, 2)), "enemies": placed}


def check_spawn_clearance(collision):
    """Warn when a spawn sits inside a proxy.

    A character spawned inside a tree is depenetrated on the first frame and
    shoots sideways, which reads as a physics bug rather than a level one.
    """
    radius = 0.5   # Player::BODY_RADIUS
    warnings = []
    markers = bpy.data.collections.get(MARKERS)
    for marker in markers.all_objects:
        mx, my = marker.location.x, marker.location.y
        for box in collision.all_objects:
            if not box.name.startswith(("BOX_Tree", "BOX_House", "BOX_Boundary")):
                continue
            hx, hy = box.scale.x * 0.5 + radius, box.scale.y * 0.5 + radius
            dx = abs(mx - box.location.x)
            dy = abs(my - box.location.y)
            if box.rotation_euler.z:
                c = math.cos(-box.rotation_euler.z)
                s = math.sin(-box.rotation_euler.z)
                rx = (mx - box.location.x) * c - (my - box.location.y) * s
                ry = (mx - box.location.x) * s + (my - box.location.y) * c
                dx, dy = abs(rx), abs(ry)
            if dx <= hx and dy <= hy:
                warnings.append("%s overlaps %s" % (marker.name, box.name))
    return warnings


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------

def parse_args(argv):
    argv = argv[argv.index("--") + 1:] if "--" in argv else []
    parser = argparse.ArgumentParser(prog="make_forest_level.py")
    parser.add_argument("--out", default="source/levels/forest.blend",
                        help="where to save the prepared .blend")
    return parser.parse_args(argv)


def main():
    args = parse_args(sys.argv)

    root, original = apply_scale()
    terrain = bpy.data.objects.get(TERRAIN)
    if terrain is None:
        raise SystemExit("no %r terrain mesh in the scene" % TERRAIN)

    visual = get_collection(VISUAL)
    collision = get_collection(COLLISION)
    markers = get_collection(MARKERS)
    reference = get_collection(REFERENCE)

    # Rebuilt every run, so the script is safe to re-run over its own output.
    clear_collection(collision)
    clear_collection(markers)

    for obj in list(bpy.data.objects):
        if obj.type in {"CAMERA", "LIGHT"}:
            continue
        if obj.name in REFERENCE_OBJECTS or (
                obj.parent is not None and obj.parent.name in REFERENCE_OBJECTS):
            move_to_collection(obj, reference)
        elif not any(c.name in (COLLISION, MARKERS) for c in obj.users_collection):
            move_to_collection(obj, visual)

    bpy.context.view_layer.update()

    ground = build_ground(collision, terrain)
    trees = build_trees(collision, terrain)
    house = build_house(collision, terrain)
    bounds = build_boundary(collision, terrain)
    spawns = build_markers(markers, terrain)

    bpy.context.view_layer.update()
    warnings = check_spawn_clearance(collision)

    out = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=out)

    # Half the X extent, not the bound_box corner: the terrain is a disc, and
    # its AABB corner sits sqrt(2) further out than any ground actually does.
    corners = [terrain.matrix_world @ Vector(c) for c in terrain.bound_box]
    span = (max(v.x for v in corners) - min(v.x for v in corners)) * 0.5
    print("\n[make_forest] scale %.0fx (import scale %.4f -> %.4f)"
          % (SCALE, original.x, root.scale.x))
    print("[make_forest] island radius %.1f m, play radius %.1f m"
          % (span, PLAY_RADIUS))
    print("[make_forest] ground: %d cells -> %d boxes, worst neighbour step "
          "%.2f m (%d over MAX_STEP %.2f)%s"
          % (ground["cells"], ground["boxes"], ground["worst_step"],
             ground["steps_over_max"], MAX_STEP,
             "" if ground["converged"] else "  [relaxation did NOT converge]"))
    if ground["error"]:
        e = ground["error"]
        print("[make_forest]   proxy vs real ground: mean %.2f m, p95 %.2f m, "
              "max %.2f m (floats %.2f, sinks %.2f)"
              % (e["mean_abs"], e["p95_abs"], e["max_abs"],
                 e["max_float"], e["max_sink"]))
    print("[make_forest] trees: %d proxies (%d outside play area)"
          % (trees["trees"], trees["outside_play_area"]))
    print("[make_forest] house: %s" % house)
    print("[make_forest] boundary: %d segments at r=%.1f m"
          % (bounds["boundary"], BOUNDARY_RADIUS))
    print("[make_forest] spawns: %s" % spawns)
    for warning in warnings:
        print("[make_forest] WARNING: %s" % warning)
    print("[make_forest] saved %s" % out)


if __name__ == "__main__":
    main()
