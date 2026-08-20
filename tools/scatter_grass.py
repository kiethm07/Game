"""Grow a meadow of generated grass blades over the green ground of a level.

Runs *after* make_castle_level.py, on the built level .blend, exactly like
seat_art.py and open_gateways.py do. It reads the `VISUAL` collection -- the
geometry that actually ships -- and writes its output into a separate `DETAIL`
collection, so it needs no knowledge of the authored art's frame, its recentre
offset, or the profile's play-area constants. Everything below is measured off
the level as built.

Which is not just convenience. The authored `landscape` in the phase .blend
sits at the origin while `VISUAL` sits in the shifted, x3 frame the level ships
in, and the two are related by a constant nobody has written down. Scattering
against `ground_east` directly would mean rediscovering that constant, and
being a metre out would put grass a metre into the path.

NO ASSET

The grass is generated here, not imported. The first version of this scattered
a photographed grass clump -- fourteen alpha-cutout quads on one atlas -- and it
was wrong in two ways that no better photograph would have fixed. Most of each
quad is transparent, so triangles bought nothing and every fragment paid an
alpha `discard`; and the colour was fixed when the texture was photographed, so
it could never agree with a terrain whose colour lives in a Blender material.

A generated blade is one triangle with no transparency, and its colour is *read
off the terrain material* (see `terrain_colour`), so it matches by construction
and goes on matching when the ground is re-tinted -- which is what
tools/tint_ground.py is for.

WHERE THE GRASS GOES

The green/yellow distinction the level reads as "meadow" and "trail" is a
material distinction: `ground_east` is one mesh with the `grass` material, and
the trail is `path1`, a separate extruded ribbon carrying `path`, laid on top
of it. So a downward ray that lands on a `grass` face is on green ground and
one that lands on a `path` face is on the trail, with no need to reason about
where the trail runs. Anything else the ray can hit first -- rock, fence rail,
castle stone -- is not ground and is rejected the same way. Canopy is the one
exception: rays pass through `tree leaves`, or nothing grows under any of the
771 trees in this phase.

Density and height are uniform: every square metre of green gets the same
meadow. There is no falloff toward the trail, and there used to be -- grass
thinned and shortened into the lane, keyed off the fence line -- but a meadow
that is the same depth everywhere reads better, so the only thing that varies
across the level now is what the ground is made of. The trail keeps itself
clear by being a different material, not by being measured against.

WHY IT IS NOT IN `VISUAL`

`DETAIL` is exported as its own `detail.glb` and named by an optional
`detailModel` key in level.json. GameRenderer draws it in the scene pass and
never mentions it in the depth pass, which is the entire implementation of
"grass casts no shadow" -- and it keeps a quarter of a million triangles out of
both shadow cascades. It also lets the two collections be exported with
different settings, which they need: `DETAIL` ships vertex colours and no
normals, `VISUAL` the other way round.

Usage:
    blender --background source/levels/phase1_forest.blend \
        --python tools/scatter_grass.py -- [--out <path>] [--dry-run]
        [--density 24] [--max-blades 400000] [--width 0.07] [--height 1.00]

Re-running is safe: every GRASS_* object is deleted from both collections and
rebuilt from the same seed, so a second run on an unchanged level produces the
same meadow rather than a second layer of it.
"""

import argparse
import math
import os
import random
import sys

import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree

# --- The level -------------------------------------------------------------

VISUAL = "VISUAL"

# Non-colliding, non-casting scenery. A sibling of VISUAL under the scene
# collection and never a child of it: export_level.py selects
# `visual.all_objects`, which recurses into child collections, so nesting this
# one would put the grass straight back into level.glb and into both shadow
# cascades while everything still appeared to work.
DETAIL = "DETAIL"

# Always-drawn distant scenery. Excluded from the scatter target for the
# obvious reason -- it is across the ravine and 200 m of it is mountainside --
# and from the ray targets too, so a ray falling off the play mesh lands on
# nothing instead of planting grass on a backdrop the player cannot reach.
BACKDROP_NAME = "BACKDROP"

GROUND_MATERIAL = "grass"
PATH_MATERIAL = "path"
WATER_MATERIAL = "water"

BLADE_MATERIAL = "grass_blade"

# Materials a scatter ray carries on through instead of stopping at. Only the
# canopy, and it is the difference between a forest floor and a bare one: a
# tree is a cone of leaves over open ground, so a ray dropped from above stops
# in the branches. The trunk is deliberately not in here. It is solid, it is
# what the ray *should* stop at, and its footprint is small enough that losing
# those blades is the correct outcome rather than a cost.
PASS_THROUGH_MATERIALS = ("tree leaves",)

# Ray restarts allowed while passing through canopy. Six is well past the four
# layers of branches the densest thicket here stacks up.
MAX_RAY_RESTARTS = 6

# Objects are named GRASS_<cell x>_<cell y> on a grid this many metres across,
# which is CHUNK_SIZE * WORLD_SCALE from make_castle_level.py -- the size the
# terrain chunks already are. The grid is anchored at the origin rather than on
# the terrain chunks' own grid: what matters for GameRenderer is that each
# mesh's AABB stays about a cell across so the frustum can reject it, not that
# the two grids line up.
CELL = 36.0

# --- The blade -------------------------------------------------------------

# One isoceles triangle: a base edge tapering to a point. A blunt-tipped quad
# (2 triangles) or a curved two-segment blade (3) were both measured against
# this and rejected -- at the game's 4.2 m camera distance the curve they buy
# deviates by about two pixels on the very nearest blades and by less than one
# past 8 m, for two to three times the triangles and the file size. The lean
# below is what actually reads as shape.
VERTS_PER_BLADE = 3
TRIS_PER_BLADE = 1

# Base width in metres, tapering to a point. Reach for this before BLADE_DENSITY
# when the meadow looks thin: coverage across a view is proportional to
# density * depth * width, so widening a blade buys the same fullness as adding
# blades and costs no triangles and no bytes at all. 7 cm is also the stylised
# proportion -- blades in this art style are chunky next to real grass.
BLADE_WIDTH = 0.07
WIDTH_JITTER = (0.85, 1.15)

# Standing height in metres against a ~1.8 m player: waist. Uniform across the
# level, lane included.
#
# Worth knowing what it costs in gameplay rather than only in triangles: at
# waist height the grass swallows the player's feet, so footing during a dodge
# has to be read off the character's upper body instead of off ground contact.
# That is the trade the look is being bought with.
BLADE_HEIGHT = 1.00
HEIGHT_JITTER = (0.65, 1.35)

# Lean off the ground normal. The blade is made longer by 1/cos(lean) so that
# the height above is a *standing* height and means what it says regardless of
# how far the blade tips over.
LEAN = (12.0, 38.0)

# Metres, absolute rather than a fraction of height: every blade gets its own
# raycast, so the exact ground point is known and this only has to hide the
# seam, not absorb a placement error.
SINK = 0.02

# --- Shading ---------------------------------------------------------------

# Vertex colours carry downward modulation ONLY -- the colour itself is the
# terrain's, in baseColorFactor. Deliberately this way round: glTF defines base
# colour as baseColorFactor * COLOR_0, so if the attribute is ever dropped
# raylib substitutes white and the grass degrades to uniform *tip* colour, which
# is exactly the terrain colour. The other way round it would degrade to white.
SHADE_TIP = 1.00
SHADE_BASE = 0.62

# Per-blade variation, so a meadow is not one flat field of the same green.
VALUE_JITTER = 0.08
HUE_JITTER = 0.05

# --- Where it grows --------------------------------------------------------

# Steeper than this and it is the ravine wall, not meadow. cos(28 degrees).
#
# There is a shading cost to reaching this far and it is worth stating: the
# blades carry no normals and light from a constant up vector (see grass.fs),
# which is what makes them shade like the ground they stand on -- and on a 28
# degree bank that approximation is about 7% out, so grass there reads slightly
# brighter than the slope under it. Held at 0.94 while the meadow was patchy,
# because the mismatch was more visible than the missing grass. Now that the
# scatter is uniform the balance flips: a bald bank in an otherwise unbroken
# meadow is the thing that catches the eye.
MIN_NORMAL_Z = 0.88

# Bare margin at the trail's edge, in metres from the trail *surface*. The
# ribbon is laid on the ground rather than cut into it, so a point at the very
# edge of the road measures ~0 -- this is a hair of clearance so that a blade
# leaning roadward does not sprout from under the tarmac, not a verge. The road
# mostly keeps itself clear by being a different material.
PATH_CLEAR = 0.3

# --- Budget ----------------------------------------------------------------

# Blades per square metre, and the number this tool exists to let you turn.
#
# Uniform now, so it is the whole level rather than a peak most of the map sat
# below: 24 costs phase1_forest roughly 300,000 triangles and a 20 MB
# detail.glb, where the same figure under the old trail falloff bought about
# half that. Reach for --width before this when the meadow looks thin -- a wider
# blade covers the same ground for no triangles at all.
BLADE_DENSITY = 24.0

# Classification samples per square metre -- NOT the blade count.
#
# The two are separate on purpose and it is the difference between a build that
# takes a minute and one that takes an hour. Classification is the expensive
# part (a BVH raycast with up to six canopy restarts, plus a nearest-surface
# query against the trail), so it runs on this coarse grid; each accepted
# sample then owns its cell and fills it with blades that each cost only the
# cheap raycast. Raising BLADE_DENSITY costs almost nothing; raising this costs
# linearly.
SITE_DENSITY = 0.45

# Ceiling for the whole level, applied as a uniform scale on the per-cell blade
# count once the classification pass has reported how much ground there is.
# Scaling rather than subsampling keeps the falloff's shape intact.
MAX_BLADES = 400000

# Blender writes u16 indices only while the largest is under 65535
# (io_scene_gltf2/blender/exp/primitives.py:218), and raylib's Mesh.indices is
# `unsigned short *` with GL_UNSIGNED_SHORT hardcoded in the draw call -- its
# glTF loader truncates a u32 index accessor mod 65536 with nothing but a
# LOG_WARNING, so the level arrives with scrambled geometry and no error. Every
# mesh built here is held under the cap and asserted.
MAX_VERTS_PER_MESH = 65534
MAX_BLADES_PER_MESH = MAX_VERTS_PER_MESH // VERTS_PER_BLADE

# Fixed so a re-run reproduces the meadow rather than reshuffling it, and so a
# diff of the exported level means something.
SEED = 20260819


class BuildError(Exception):
    """Something about this level is not what the scatter needs."""


# ---------------------------------------------------------------------------
# Blender helpers
# ---------------------------------------------------------------------------

def collection(name):
    found = bpy.data.collections.get(name)
    if found is None:
        raise BuildError("no %r collection -- this is not a built level "
                         ".blend. Run make_castle_level.py first." % name)
    return found


def detail_collection():
    """The DETAIL collection, linked directly under the scene collection.

    Checked rather than assumed, because the one arrangement that breaks this
    silently -- DETAIL nested inside VISUAL -- leaves everything looking
    correct in Blender while putting the whole meadow back into level.glb.
    """
    scene = bpy.context.scene.collection
    found = bpy.data.collections.get(DETAIL)
    if found is None:
        found = bpy.data.collections.new(DETAIL)
        scene.children.link(found)
        return found

    for parent in bpy.data.collections:
        if found.name in {c.name for c in parent.children}:
            raise BuildError(
                "%r is nested inside %r. It has to sit directly under the "
                "scene collection: export_level.py selects VISUAL's objects "
                "recursively, so a nested DETAIL exports into level.glb and "
                "into both shadow cascades." % (DETAIL, parent.name))
    if found.name not in {c.name for c in scene.children}:
        scene.children.link(found)
    return found


def material_names(obj):
    return [m.name if m else "" for m in obj.data.materials]


def triangles_of(obj, depsgraph):
    """(world verts, (i, j, k), material name) for every triangle of `obj`."""
    evaluated = obj.evaluated_get(depsgraph)
    mesh = evaluated.to_mesh()
    if mesh is None:
        return [], []
    try:
        mesh.calc_loop_triangles()
        matrix = obj.matrix_world
        names = material_names(obj)
        verts = [matrix @ v.co for v in mesh.vertices]
        faces = []
        for tri in mesh.loop_triangles:
            index = tri.material_index
            faces.append((tuple(tri.vertices),
                          names[index] if index < len(names) else ""))
        return verts, faces
    finally:
        evaluated.to_mesh_clear()


def scatter_targets(visual):
    """Everything a scatter ray may hit, as one BVH plus a per-face material.

    One tree over the whole level rather than a tree per material: the test
    that matters is what the ray hits *first*, so a canopy has to be in the
    same tree as the ground under it or the grass grows through it.
    """
    depsgraph = bpy.context.evaluated_depsgraph_get()
    verts, polys, mats = [], [], []
    for obj in sorted(visual.all_objects, key=lambda o: o.name):
        if obj.type != "MESH" or obj.name == BACKDROP_NAME:
            continue
        base = len(verts)
        obj_verts, obj_faces = triangles_of(obj, depsgraph)
        verts.extend(obj_verts)
        for (i, j, k), name in obj_faces:
            polys.append((base + i, base + j, base + k))
            mats.append(name)
    if not polys:
        raise BuildError("VISUAL holds no mesh geometry besides the backdrop.")
    return BVHTree.FromPolygons(verts, polys, all_triangles=True), mats


def material_tree(visual, wanted):
    """A BVH over just the faces carrying `wanted`, for distance queries."""
    depsgraph = bpy.context.evaluated_depsgraph_get()
    verts, polys = [], []
    for obj in sorted(visual.all_objects, key=lambda o: o.name):
        if obj.type != "MESH" or obj.name == BACKDROP_NAME:
            continue
        if wanted not in material_names(obj):
            continue
        base = len(verts)
        obj_verts, obj_faces = triangles_of(obj, depsgraph)
        verts.extend(obj_verts)
        for (i, j, k), name in obj_faces:
            if name == wanted:
                polys.append((base + i, base + j, base + k))
    if not polys:
        return None
    return BVHTree.FromPolygons(verts, polys, all_triangles=True)


def material_points(visual, wanted):
    """World-space centroids of every face carrying `wanted`."""
    depsgraph = bpy.context.evaluated_depsgraph_get()
    points = []
    for obj in sorted(visual.all_objects, key=lambda o: o.name):
        if obj.type != "MESH" or obj.name == BACKDROP_NAME:
            continue
        if wanted not in material_names(obj):
            continue
        verts, faces = triangles_of(obj, depsgraph)
        for (i, j, k), name in faces:
            if name == wanted:
                points.append((verts[i] + verts[j] + verts[k]) / 3.0)
    return points


# ---------------------------------------------------------------------------
# Colour
# ---------------------------------------------------------------------------

def terrain_colour():
    """The ground's own Base Color, which the blades then share.

    Read rather than declared. A grass colour typed into this file is a second
    answer to a question the terrain material already answers, and the two
    drifting apart is exactly the fault this rewrite exists to fix -- so a
    node-driven Base Color is refused outright rather than guessed at.
    """
    material = bpy.data.materials.get(GROUND_MATERIAL)
    if material is None or not material.use_nodes:
        raise BuildError("no %r material with a node tree to take the meadow "
                         "colour from." % GROUND_MATERIAL)
    bsdf = next((n for n in material.node_tree.nodes
                 if n.type == "BSDF_PRINCIPLED"), None)
    if bsdf is None:
        raise BuildError("%r has no Principled BSDF." % GROUND_MATERIAL)
    socket = bsdf.inputs["Base Color"]
    if socket.links:
        raise BuildError(
            "%r drives Base Color from a %s node, so there is no single colour "
            "to match the grass to. Flatten it -- tools/tint_ground.py sets it "
            "-- and re-run." % (GROUND_MATERIAL, socket.links[0].from_node.type))
    return tuple(socket.default_value)


def blade_material(colour):
    """One material for the meadow: the terrain's colour, flat, untextured.

    Untextured on purpose. export_level.py leaves an unlinked Base Color alone,
    so this reaches the .glb as a plain baseColorFactor and grass.fs multiplies
    it by the vertex shade -- which means a lit blade tip on flat ground comes
    out byte-identical to the terrain pixel beside it. Any texture in the path
    would break that equality, and the equality is the point.
    """
    for stale in (BLADE_MATERIAL, "grass_green"):
        existing = bpy.data.materials.get(stale)
        if existing is not None:
            bpy.data.materials.remove(existing)
    # The photographic atlas the first version packed into the .blend.
    for image in list(bpy.data.images):
        if image.name.startswith("grass_green") and image.users == 0:
            bpy.data.images.remove(image)

    material = bpy.data.materials.new(BLADE_MATERIAL)
    material.use_nodes = True
    bsdf = next(n for n in material.node_tree.nodes
                if n.type == "BSDF_PRINCIPLED")
    bsdf.inputs["Base Color"].default_value = colour
    bsdf.inputs["Roughness"].default_value = 0.9
    bsdf.inputs["Metallic"].default_value = 0.0
    # Viewport only, but it makes the .blend readable at a glance.
    material.diffuse_color = colour
    return material


# ---------------------------------------------------------------------------
# The ground
# ---------------------------------------------------------------------------

def water_level(visual):
    """Top of the water, or None. Grass does not grow in the ravine."""
    tops = [p.z for p in material_points(visual, WATER_MATERIAL)]
    backdrop = bpy.data.objects.get(BACKDROP_NAME)
    if backdrop is not None and WATER_MATERIAL in material_names(backdrop):
        names = material_names(backdrop)
        matrix = backdrop.matrix_world
        for poly in backdrop.data.polygons:
            if names[poly.material_index] == WATER_MATERIAL:
                tops.append((matrix @ poly.center).z)
    return max(tops) if tops else None


def ground_bounds(visual):
    """Plan extent of the green ground, which is where the scatter may reach.

    Taken off the geometry rather than from the profile's BOUNDARY_RECT: the
    terrain runs some way past the invisible wall on every side, and grass that
    stops exactly at the wall draws a straight edge across a forest.
    """
    points = material_points(visual, GROUND_MATERIAL)
    if not points:
        raise BuildError("no %r faces in VISUAL -- nothing to grow grass on."
                         % GROUND_MATERIAL)
    xs = [p.x for p in points]
    ys = [p.y for p in points]
    zs = [p.z for p in points]
    return (min(xs), min(ys)), (max(xs), max(ys)), max(zs)


class Ground(object):
    """The level's surface, as the two queries the scatter needs of it.

    Built once and shared by both passes: the classification grid and the
    per-blade drop use the same BVH, so a blade can never be placed against a
    different surface than the one its cell was classified from.
    """

    def __init__(self, visual):
        self.targets, self.face_material = scatter_targets(visual)
        self.path = material_tree(visual, PATH_MATERIAL)
        self.water = water_level(visual)
        (self.min_x, self.min_y), (self.max_x, self.max_y), top = \
            ground_bounds(visual)
        # Well clear of the tallest canopy: a ray that starts inside a tree
        # misses it and plants grass under the branches.
        self.origin_z = top + 15.0
        self.down = Vector((0.0, 0.0, -1.0))

    def drop(self, x, y):
        """First thing under (x, y) that is not canopy.

        Returns (hit, normal, material, restarts); hit is None off the mesh.
        """
        origin = Vector((x, y, self.origin_z))
        for restart in range(MAX_RAY_RESTARTS):
            hit, normal, index, _ = self.targets.ray_cast(origin, self.down)
            if hit is None:
                return None, None, None, restart
            if self.face_material[index] in PASS_THROUGH_MATERIALS:
                # Resume just below the leaf that was hit, or the same face is
                # found again and the loop makes no progress.
                origin = hit - Vector((0.0, 0.0, 1e-3))
                continue
            return hit, normal, self.face_material[index], restart
        return None, None, None, MAX_RAY_RESTARTS


# ---------------------------------------------------------------------------
# Pass 1 -- classification
# ---------------------------------------------------------------------------

def classify(ground, spacing, rng, report):
    """Which cells of the level are meadow.

    One sample per `spacing` cell, and the answer is now yes or no rather than
    a depth: every cell that survives gets the same meadow as every other. The
    expensive queries all live here -- a BVH raycast with up to six canopy
    restarts and a nearest-surface query against the trail -- which is why the
    grid stays coarse and the blades themselves are placed in a second, cheap
    pass.
    """
    counts = {"samples": 0, "missed": 0, "not_ground": 0, "steep": 0,
              "on_path": 0, "drowned": 0, "under_canopy": 0}
    cells = []

    steps_x = int((ground.max_x - ground.min_x) / spacing) + 1
    steps_y = int((ground.max_y - ground.min_y) / spacing) + 1
    for ix in range(steps_x):
        for iy in range(steps_y):
            counts["samples"] += 1
            cell_x = ground.min_x + ix * spacing
            cell_y = ground.min_y + iy * spacing
            x = cell_x + rng.random() * spacing
            y = cell_y + rng.random() * spacing
            hit, normal, material, restarts = ground.drop(x, y)
            if restarts:
                counts["under_canopy"] += 1
            if hit is None:
                counts["missed"] += 1
                continue
            if material != GROUND_MATERIAL:
                counts["not_ground"] += 1
                continue
            if normal.z < MIN_NORMAL_Z:
                counts["steep"] += 1
                continue
            if ground.water is not None and hit.z < ground.water:
                counts["drowned"] += 1
                continue

            if ground.path is not None:
                _, _, _, distance = ground.path.find_nearest(hit)
                if distance is None or distance < PATH_CLEAR:
                    counts["on_path"] += 1
                    continue
            cells.append((cell_x, cell_y))

    report["classify"] = counts
    return cells


# ---------------------------------------------------------------------------
# Pass 2 -- the blades
# ---------------------------------------------------------------------------

def blade_frame(normal, azimuth):
    """(lean direction, half-width direction), both in the ground plane.

    The half-width runs perpendicular to the lean, so a blade that has tipped
    over shows its face to a viewer standing beside it. Lay them the other way
    and a leaning blade presents its edge, and the lean stops reading at all.
    """
    # `normal.z >= MIN_NORMAL_Z` (0.94) throughout, so world +Y is never close
    # to parallel with it and this cross product cannot degenerate.
    first = Vector((0.0, 1.0, 0.0)).cross(normal).normalized()
    second = normal.cross(first)
    lean = (first * math.cos(azimuth) + second * math.sin(azimuth)).normalized()
    return lean, lean.cross(normal).normalized()


def shade_colour(level, tint):
    """One COLOR_0 value: neutral modulation, never the colour itself.

    glTF multiplies baseColorFactor by COLOR_0, and baseColorFactor already
    carries the terrain green -- so putting the green in here as well would
    square it. What goes in is how *dark* this point of the blade is, nudged
    per blade so the meadow is not one flat sheet of a single value.
    """
    return (max(0.0, min(1.0, level * tint[0])),
            max(0.0, min(1.0, level * tint[1])),
            max(0.0, min(1.0, level * tint[2])))


def grow_blades(ground, cells, spacing, rng, options, report):
    """A blade list: (x, y, three world verts, three vertex colours).

    Each classified cell is filled independently, and every blade inside it
    gets its own downward ray. That ray is the reason blades sit flush across a
    terrain crease: the mesh runs about 0.6 triangles per square metre, so a
    1.5 m cell straddles an edge routinely and extrapolating from the cell's
    own hit triangle would float or bury blades by 10-20 cm.
    """
    cell_area = spacing * spacing
    projected = options["density"] * cell_area * len(cells)
    scale = 1.0 if projected <= options["max_blades"] \
        else options["max_blades"] / projected
    report["projected"] = projected
    report["scale"] = scale

    # Uniform, so the per-cell count is the same everywhere and only its
    # fractional part is resolved per cell. Stochastic rounding rather than a
    # floor, or a density of 24.6 would scatter 24 and quietly lose 2.5%.
    wanted = options["density"] * cell_area * scale
    whole, fraction = int(wanted), wanted - int(wanted)

    counts = {"tried": 0, "missed": 0, "not_ground": 0, "steep": 0,
              "drowned": 0}
    blades = []
    step = max(1, len(cells) // 10)

    for index, (cell_x, cell_y) in enumerate(cells):
        if index and index % step == 0:
            print("[scatter_grass]   ...%3d%%  %d blades"
                  % (100 * index // len(cells), len(blades)))

        count = whole + (1 if rng.random() < fraction else 0)
        for _ in range(count):
            counts["tried"] += 1
            x = cell_x + rng.random() * spacing
            y = cell_y + rng.random() * spacing
            hit, normal, material, _ = ground.drop(x, y)
            if hit is None:
                counts["missed"] += 1
                continue
            if material != GROUND_MATERIAL:
                counts["not_ground"] += 1
                continue
            if normal.z < MIN_NORMAL_Z:
                counts["steep"] += 1
                continue
            if ground.water is not None and hit.z < ground.water:
                counts["drowned"] += 1
                continue

            lean_angle = math.radians(rng.uniform(*LEAN))
            direction, side = blade_frame(normal, rng.uniform(0.0, math.tau))
            half = side * (options["width"] * rng.uniform(*WIDTH_JITTER) * 0.5)
            # Length, not height: dividing by cos(lean) keeps the tip at the
            # asked-for height above the ground however far the blade tips.
            length = (options["height"]
                      * rng.uniform(*HEIGHT_JITTER)) / math.cos(lean_angle)

            foot = hit - normal * SINK
            tip = hit + normal * (length * math.cos(lean_angle)) \
                + direction * (length * math.sin(lean_angle))

            value = 1.0 + rng.uniform(-VALUE_JITTER, VALUE_JITTER)
            hue = rng.uniform(-HUE_JITTER, HUE_JITTER)
            tint = (value * (1.0 - 0.8 * hue), value * (1.0 + hue), value)
            base = shade_colour(SHADE_BASE, tint) + (0.0,)
            crown = shade_colour(SHADE_TIP, tint) + (1.0,)

            blades.append((hit.x, hit.y,
                           ((foot - half)[:], (foot + half)[:], tip[:]),
                           (base, base, crown)))

    report["grow"] = counts
    report["blades"] = len(blades)
    return blades


# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

def clear_previous(collections):
    """Remove every GRASS_* object, from VISUAL as well as DETAIL.

    VISUAL as well because the first version of this tool put its tufts there.
    Sweeping only DETAIL would leave the old photographic grass in the level
    permanently, sitting under the new meadow.
    """
    removed = 0
    for group in collections:
        for obj in [o for o in list(group.all_objects)
                    if o.name.startswith("GRASS_")]:
            data = obj.data
            bpy.data.objects.remove(obj, do_unlink=True)
            if data is not None and data.users == 0:
                bpy.data.meshes.remove(data)
            removed += 1
    return removed


def cell_of(x, y):
    return int(math.floor(x / CELL)), int(math.floor(y / CELL))


def build_chunks(blades, material, detail, report):
    """Merge the blades into meshes, one per grid cell, split under the cap.

    Per cell rather than per blade because there is no instancing anywhere in
    GameRenderer -- every mesh is one DrawMesh -- and per cell rather than one
    mesh for the level because a level-sized AABB passes every frustum test
    there is. The split within a cell is contiguous over blades already sorted
    by position, so each piece is a strip with a strip-shaped bounding box the
    frustum can still reject; splitting round-robin would give every piece the
    whole cell's box and buy nothing.
    """
    by_cell = {}
    for blade in blades:
        by_cell.setdefault(cell_of(blade[0], blade[1]), []).append(blade)

    built, widest_verts, splits = [], 0, 0
    for cell in sorted(by_cell):
        members = sorted(by_cell[cell],
                         key=lambda b: (round(b[0], 3), round(b[1], 3)))
        groups = [members[i:i + MAX_BLADES_PER_MESH]
                  for i in range(0, len(members), MAX_BLADES_PER_MESH)]
        if len(groups) > 1:
            splits += len(groups) - 1

        for part, group in enumerate(groups):
            name = "GRASS_%+04d_%+04d" % cell
            if len(groups) > 1:
                name += "_%d" % part

            verts, tris, colours = [], [], []
            for blade in group:
                base = len(verts)
                verts.extend(blade[2])
                colours.extend(blade[3])
                tris.append((base, base + 1, base + 2))

            if len(verts) > MAX_VERTS_PER_MESH:
                raise BuildError(
                    "%s came out at %d vertices, over the %d the glTF exporter "
                    "will still index with u16. raylib truncates anything wider "
                    "mod 65536 and renders scrambled geometry with no error."
                    % (name, len(verts), MAX_VERTS_PER_MESH))
            widest_verts = max(widest_verts, len(verts))

            mesh = bpy.data.meshes.new(name)
            mesh.from_pydata(verts, [], tris)
            mesh.validate()

            # FLOAT_COLOR on POINT: per-corner would hold the same value three
            # times and only invite the exporter to split vertices, and float
            # storage has no 8-bit sRGB round trip to reason about.
            attribute = mesh.color_attributes.new(
                name="grass_shade", type="FLOAT_COLOR", domain="POINT")
            flat = []
            for colour in colours:
                flat.extend(colour)
            attribute.data.foreach_set("color", flat)
            # The exporter reads the RENDER index, not the active one
            # (io_scene_gltf2/blender/exp/primitive_extract.py:531). Both are
            # set because getting this wrong ships flat grass and no error.
            index = mesh.color_attributes.find("grass_shade")
            mesh.color_attributes.render_color_index = index
            mesh.color_attributes.active_color_index = index

            mesh.materials.append(material)
            mesh.update()

            obj = bpy.data.objects.new(name, mesh)
            detail.objects.link(obj)
            built.append(obj)

    report["chunks"] = len(built)
    report["splits"] = splits
    report["widest_verts"] = widest_verts
    return built


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------

def parse_args(argv):
    argv = argv[argv.index("--") + 1:] if "--" in argv else []
    parser = argparse.ArgumentParser(prog="scatter_grass.py")
    parser.add_argument("--out", default=None,
                        help="where to save (defaults to the input .blend)")
    parser.add_argument("--dry-run", action="store_true",
                        help="build and report, but do not write the .blend")
    parser.add_argument("--density", type=float, default=BLADE_DENSITY,
                        help="blades per square metre at full density "
                             "(default %.1f)" % BLADE_DENSITY)
    parser.add_argument("--max-blades", type=int, default=MAX_BLADES,
                        help="ceiling for the level (default %d)" % MAX_BLADES)
    parser.add_argument("--width", type=float, default=BLADE_WIDTH,
                        help="blade base width in metres (default %.3f). "
                             "Cheaper than density for the same fullness."
                             % BLADE_WIDTH)
    parser.add_argument("--height", type=float, default=BLADE_HEIGHT,
                        help="standing blade height in metres, uniform across "
                             "the level (default %.2f -- waist on a 1.8 m "
                             "player)" % BLADE_HEIGHT)
    return parser.parse_args(argv)


def main():
    args = parse_args(sys.argv)

    visual = collection(VISUAL)
    detail = detail_collection()
    report = {"removed": clear_previous((visual, detail))}

    colour = terrain_colour()
    material = blade_material(colour)

    rng = random.Random(SEED)
    spacing = 1.0 / math.sqrt(SITE_DENSITY)
    ground = Ground(visual)

    cells = classify(ground, spacing, rng, report)
    if not cells:
        raise BuildError(
            "no cell of this level classified as meadow. Check that VISUAL "
            "still carries %r faces and that they are not all steeper than "
            "MIN_NORMAL_Z (%.2f)." % (GROUND_MATERIAL, MIN_NORMAL_Z))
    report["area"] = len(cells) / SITE_DENSITY

    options = {"density": args.density, "max_blades": args.max_blades,
               "width": args.width, "height": args.height}
    blades = grow_blades(ground, cells, spacing, rng, options, report)
    if not blades:
        raise BuildError("every blade was rejected after classification "
                         "accepted %d cells; this should not happen."
                         % len(cells))
    build_chunks(blades, material, detail, report)

    tris = report["blades"] * TRIS_PER_BLADE
    verts = report["blades"] * VERTS_PER_BLADE
    classified = report["classify"]
    grown = report["grow"]

    print("\n[scatter_grass] colour     (%.3f, %.3f, %.3f) from the %r material"
          % (colour[0], colour[1], colour[2], GROUND_MATERIAL))
    print("[scatter_grass] blade      %.2f m tall (x%.2f..%.2f), %.0f cm wide, "
          "leaning %.0f-%.0f deg"
          % (args.height, HEIGHT_JITTER[0], HEIGHT_JITTER[1],
             args.width * 100.0, LEAN[0], LEAN[1]))
    print("[scatter_grass] falloff    none -- uniform over every green cell, "
          "%.1f m clear of the trail" % PATH_CLEAR)
    print("[scatter_grass] classify   %d samples -> %d meadow cells = %.0f m^2 "
          "(off-mesh %d, not ground %d, too steep %d, on the trail %d, "
          "drowned %d; %d through canopy)"
          % (classified["samples"], len(cells), report["area"],
             classified["missed"], classified["not_ground"],
             classified["steep"], classified["on_path"], classified["drowned"],
             classified["under_canopy"]))
    if report["scale"] < 1.0:
        print("[scatter_grass] ceiling    %.0f blades wanted, scaled to %.0f%% "
              "by --max-blades %d"
              % (report["projected"], report["scale"] * 100.0,
                 args.max_blades))
    print("[scatter_grass] grow       %d placed of %d tried (off-mesh %d, not "
          "ground %d, too steep %d, drowned %d)"
          % (report["blades"], grown["tried"], grown["missed"],
             grown["not_ground"], grown["steep"], grown["drowned"]))
    print("[scatter_grass] blades     %d = %.1f/m^2, %d triangles, %d vertices"
          % (report["blades"], report["blades"] / report["area"], tris, verts))
    print("[scatter_grass] chunks     %d meshes (%d from splitting), widest "
          "%d verts = %.0f%% of the %d cap"
          % (report["chunks"], report["splits"], report["widest_verts"],
             100.0 * report["widest_verts"] / MAX_VERTS_PER_MESH,
             MAX_VERTS_PER_MESH))
    # POSITION 12 B + COLOR_0 8 B per vertex, u16 indices 2 B each.
    print("[scatter_grass] detail.glb about %.1f MB"
          % ((verts * 20 + tris * 3 * 2) / 1048576.0))
    if report["removed"]:
        print("[scatter_grass] replaced   %d GRASS_* object(s) from a previous "
              "run" % report["removed"])

    if args.dry_run:
        print("[scatter_grass] dry run -- not saved")
        return

    out = args.out or bpy.data.filepath
    if not out:
        raise BuildError("no --out and the .blend has no path to save back to.")
    bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(out))
    print("[scatter_grass] saved      %s" % out)


if __name__ == "__main__":
    try:
        main()
    except BuildError as error:
        raise SystemExit("[scatter_grass] %s" % error)
