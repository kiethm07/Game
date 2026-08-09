"""Shared machinery for turning terrain art into PhysicsObstacle proxies.

Extracted from tools/make_forest_level.py so the castle approach can use it
without a second copy. The algorithms and the reasoning behind them are that
script's; what changed here is only that the tunables arrive as arguments
instead of module constants, because the two levels want different ones.

Nothing in here is level-specific, and nothing in here writes a file. Callers
own the collections, the naming, and the numbers.

The engine-side facts these encode, which are the parts that must not drift:

  * PhysicsObstacle is axis-aligned boxes and rectangular ramps, nothing else.
  * PhysicsManager treats a proxy top up to MAX_STEP (0.3 m,
    PhysicsManager.cpp:176) above the feet as a floor to step onto, and
    anything higher as a wall. Every neighbouring pair of ground boxes has to
    stay inside that budget or the level grows invisible knee-high walls.
  * export_box reads an object's bound_box and multiplies by its scale, so a
    proxy object's scale *is* the box's size in metres.
"""

import math

import bpy
from mathutils import Vector


# ---------------------------------------------------------------------------
# Collections
# ---------------------------------------------------------------------------

def get_collection(name):
    """Fetch or create a collection linked to the scene root."""
    existing = bpy.data.collections.get(name)
    if existing is None:
        existing = bpy.data.collections.new(name)
    if existing.name not in bpy.context.scene.collection.children:
        bpy.context.scene.collection.children.link(existing)
    return existing


def move_to_collection(obj, collection):
    for c in list(obj.users_collection):
        c.objects.unlink(obj)
    collection.objects.link(obj)


def clear_collection(collection):
    for obj in list(collection.all_objects):
        bpy.data.objects.remove(obj, do_unlink=True)


# ---------------------------------------------------------------------------
# Proxy objects
# ---------------------------------------------------------------------------

def unit_cube_mesh():
    """A 1x1x1 cube centred on its origin, shared by every box proxy.

    export_box reads obj.bound_box (local, so +-0.5 here) and multiplies by the
    object's scale, which makes the object's scale the box's full size in
    metres.
    """
    mesh = bpy.data.meshes.get("__proxy_cube")
    if mesh is not None:
        return mesh
    mesh = bpy.data.meshes.new("__proxy_cube")
    h = 0.5
    verts = [(-h, -h, -h), (h, -h, -h), (h, h, -h), (-h, h, -h),
             (-h, -h, h), (h, -h, h), (h, h, h), (-h, h, h)]
    faces = [(0, 1, 2, 3), (4, 7, 6, 5), (0, 4, 5, 1),
             (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)]
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    return mesh


def add_box(collection, name, centre, size, yaw_deg, colour):
    """A BOX_ proxy. `centre` and `size` are metres in Blender space."""
    obj = bpy.data.objects.new(name, unit_cube_mesh())
    obj.location = centre
    obj.scale = size
    obj.rotation_euler = (0.0, 0.0, math.radians(yaw_deg))
    obj.color = colour
    collection.objects.link(obj)
    return obj


def add_marker(collection, name, location, yaw_deg):
    obj = bpy.data.objects.new(name, None)
    obj.empty_display_type = "ARROWS"
    obj.empty_display_size = 2.0
    obj.location = location
    obj.rotation_euler = (0.0, 0.0, math.radians(yaw_deg))
    collection.objects.link(obj)
    return obj


def game_yaw(from_xy, to_xy):
    """Degrees about game Y that face `from_xy` at `to_xy`.

    Blender XY maps to game XZ through to_game's (x, y, z) -> (x, z, -y), so the
    look vector's game components are (dx, -dy). The engine reads a yaw t as
    forward (sin t, 0, cos t) (Player.cpp:219), hence atan2(x, z).
    """
    dx = to_xy[0] - from_xy[0]
    dz = -(to_xy[1] - from_xy[1])
    return math.degrees(math.atan2(dx, dz))


# ---------------------------------------------------------------------------
# Terrain sampling
# ---------------------------------------------------------------------------

def terrain_height(terrain, x, y):
    """Blender Z of the ground under (x, y), or None off the mesh."""
    inv = terrain.matrix_world.inverted()
    origin = inv @ Vector((x, y, 1000.0))
    direction = inv.to_3x3() @ Vector((0.0, 0.0, -1.0))
    hit, location, _, _ = terrain.ray_cast(origin, direction)
    if not hit:
        return None
    return (terrain.matrix_world @ location).z


def sample_ground_grid(terrain, radius, cell, samples, centre=(0.0, 0.0)):
    """Mean terrain height per grid cell.

    Returns (heights, n, origin) where heights[j][i] is None for cells that
    miss the mesh or fall outside `radius` of `centre`, and origin is the
    (x, y) of the grid's low corner.
    """
    n = int(math.ceil(2.0 * radius / cell))
    origin = (centre[0] - 0.5 * n * cell, centre[1] - 0.5 * n * cell)
    heights = [[None] * n for _ in range(n)]

    for j in range(n):
        for i in range(n):
            x0 = origin[0] + i * cell
            y0 = origin[1] + j * cell
            cx, cy = x0 + cell * 0.5, y0 + cell * 0.5
            if math.hypot(cx - centre[0], cy - centre[1]) > radius:
                continue

            hits = []
            for sj in range(samples):
                for si in range(samples):
                    sx = x0 + cell * (si + 0.5) / samples
                    sy = y0 + cell * (sj + 0.5) / samples
                    z = terrain_height(terrain, sx, sy)
                    if z is not None:
                        hits.append(z)
            if not hits:
                continue
            heights[j][i] = sum(hits) / len(hits)

    return heights, n, origin


def quantize_and_relax(heights, n, quantum, step_quanta, iterations,
                       cliff_quanta=None):
    """Snap heights to `quantum`, then pull neighbouring cells together until
    none differ by more than `step_quanta` of them. Modifies in place.

    `cliff_quanta`, when given, is the point past which a height difference is
    read as a deliberate feature rather than an artefact, and is left alone.
    Relaxation exists to remove the half-metre bumps that stylised low-poly
    terrain puts in the middle of ground that is meant to read as flat. It is
    not meant to be applied to a ravine: a 7 m drop cannot be brought inside a
    0.25 m step budget without spreading it over thirty cells, which does not
    smooth the cliff so much as demolish the map around it. Left unrelaxed, the
    drop stays a drop -- and because PhysicsManager treats any neighbour more
    than MAX_STEP above the feet as a wall rather than a stair, the cliff
    becomes collision that behaves like a cliff for free.

    Symmetric on purpose. Raising only the low cell would build an upper
    envelope the character floats on top of; lowering only the high cell would
    bury its feet on every rise. Splitting the difference keeps the surface
    centred on the terrain, so the error shows up as half a bump either way
    rather than a whole one in a fixed direction.

    Runs in whole multiples of `quantum` rather than in metres. Heights have to
    be quantised anyway -- it is what lets equal cells merge into big rectangles
    instead of staying hundreds of individual boxes -- and quantising *after* a
    relaxation in metres can push a pair that just satisfied the limit back over
    it by up to one quantum. Relaxing in the quantised domain makes every
    difference an exact integer, so the constraint is enforced exactly instead
    of nearly.
    """
    units = [[None if v is None else int(round(v / quantum)) for v in row]
             for row in heights]

    settled = False
    for _ in range(iterations):
        moved = False
        for j in range(n):
            for i in range(n):
                if units[j][i] is None:
                    continue
                for dj, di in ((0, 1), (1, 0)):
                    nj, ni = j + dj, i + di
                    if nj >= n or ni >= n or units[nj][ni] is None:
                        continue
                    diff = units[nj][ni] - units[j][i]
                    if abs(diff) <= step_quanta:
                        continue
                    if cliff_quanta is not None and abs(diff) > cliff_quanta:
                        continue
                    # One quantum off each end, so the pair closes by two and
                    # neither cell is favoured over the other.
                    step = 1 if diff > 0 else -1
                    units[j][i] += step
                    units[nj][ni] -= step
                    moved = True
        if not moved:
            settled = True
            break

    for j in range(n):
        for i in range(n):
            heights[j][i] = (None if units[j][i] is None
                             else units[j][i] * quantum)
    return settled


def merge_rectangles(heights, n):
    """Greedily cover equal-height cells with maximal rectangles.

    Straight boxes-per-cell would be hundreds of proxies for a hillside that is
    flat across most of its area; merging collapses the flat middle into a
    handful.
    """
    used = [[False] * n for _ in range(n)]
    rects = []

    for j in range(n):
        for i in range(n):
            if used[j][i] or heights[j][i] is None:
                continue
            h = heights[j][i]

            width = 0
            while (i + width < n and not used[j][i + width]
                   and heights[j][i + width] == h):
                width += 1

            height = 0
            while j + height < n:
                row = j + height
                if any(used[row][i + k] or heights[row][i + k] != h
                       for k in range(width)):
                    break
                height += 1

            for row in range(j, j + height):
                for col in range(i, i + width):
                    used[row][col] = True
            rects.append((i, j, width, height, h))

    return rects


def report_steps(heights, n, max_step, cliff=None):
    """Height differences between neighbouring cells.

    Returns (worst, over, cliffs). Anything over `max_step` is a place the
    character walks into an invisible wall instead of up a slope, so it is
    worth seeing rather than discovering in game -- except where that wall is
    the point. Differences above `cliff` are counted separately rather than
    reported as faults, so the number that needs to reach zero stays
    meaningful on a map that has real drops in it.
    """
    worst = 0.0
    over = 0
    cliffs = 0
    for j in range(n):
        for i in range(n):
            h = heights[j][i]
            if h is None:
                continue
            for dj, di in ((0, 1), (1, 0)):
                nj, ni = j + dj, i + di
                if nj >= n or ni >= n:
                    continue
                other = heights[nj][ni]
                if other is None:
                    continue
                step = abs(other - h)
                if cliff is not None and step > cliff + 1e-6:
                    cliffs += 1
                    continue
                worst = max(worst, step)
                if step > max_step + 1e-6:
                    over += 1
    return worst, over, cliffs


def measure_error(terrain, heights, n, origin, cell):
    """How far the finished proxy surface sits from the ground it stands in for.

    Sampled off-centre from the cell centres the heights were built from, so it
    reports the error the player actually walks on rather than the one the
    construction optimised for.
    """
    errors = []
    for j in range(n):
        for i in range(n):
            if heights[j][i] is None:
                continue
            for fy in (0.25, 0.75):
                for fx in (0.25, 0.75):
                    x = origin[0] + (i + fx) * cell
                    y = origin[1] + (j + fy) * cell
                    z = terrain_height(terrain, x, y)
                    if z is not None:
                        errors.append(heights[j][i] - z)
    if not errors:
        return {}
    absolute = sorted(abs(e) for e in errors)
    return {"mean_abs": sum(absolute) / len(absolute),
            "p95_abs": absolute[int(0.95 * (len(absolute) - 1))],
            "max_abs": absolute[-1],
            "max_float": max(errors), "max_sink": -min(errors)}
