"""Turn the kitbashed castle-approach art into geometry the engine can afford.

This is the exterior of the main map -- phases 1 and 2, the forest island and
the walled compound you cross a bridge to reach. As authored it is roughly
641,000 triangles across 2,822 objects, which would arrive in the renderer as
~5,200 draw calls. The triangles are affordable; the draw calls are not.
GameRenderer walks every mesh and AABB-tests it once for the camera and once
per shadow cascade (ShadowMap::kCascadeCount is 2), then issues one DrawMesh
per survivor
(GameRenderer.cpp:317 and :493). There is no GPU instancing anywhere in the
path.

So this script's job is not really "decimate". It is:

    1. stop exporting things that are not level geometry,
    2. spend triangles where the camera can see them,
    3. and collapse thousands of small objects into a few hundred meshes whose
       bounding boxes are still tight enough to cull.

That last one has a failure mode in both directions. Too many meshes and the
per-mesh loops dominate; too few and a merged mesh gets a map-sized AABB, never
culls, and is drawn in full every frame. Hence a fixed-size spatial grid rather
than "join everything that shares a material".

Usage:
    blender --background source/levels/castle_approach.blend \
        --python tools/make_castle_level.py -- [--out <path.blend>]

Then export it, exactly as for any other level:
    blender --background source/levels/castle_approach.blend \
        --python tools/export_level.py -- --out-dir assets/levels/castle_approach
    python3 tools/verify_level.py assets/levels/castle_approach

Re-running is safe, and it is safe for a reason worth stating: this script never
edits the authored art. The collections the map was kitbashed in (`landscape`,
`finals buildings`, `final rocks`, and the off-stage kit library) are read and
left exactly as they are. VISUAL is deleted and regenerated from them on every
run. That is what makes every step below expressible as "set to" rather than
"multiply by" -- a second run decimates the same original mesh to the same
ratio, instead of decimating the already-decimated result.
"""

import argparse
import math
import os
import sys
from collections import defaultdict

import bpy
import bmesh
from mathutils import Vector

# Blender does not put a --python script's own directory on sys.path.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from level_terrain import (          # noqa: E402
    add_box, add_marker, game_yaw, get_collection, terrain_height,
)

# --- Collections ------------------------------------------------------------

VISUAL = "VISUAL"
COLLISION = "COLLISION"
MARKERS = "MARKERS"
COLLISION_MESH = "COLLISION_MESH"

# Authored art. Read-only to this script, and deliberately not exported: they
# are the source VISUAL is regenerated from.
LANDSCAPE = "landscape"
BUILDINGS = "finals buildings"
ROCKS = "final rocks"

# The kit library the `finals` collections were built from, parked at z ~= -24,
# 25 m below the map. 67,078 triangles and 62 draw calls of content that is not
# in the level at all -- it just has to not end up in VISUAL.
LIBRARY = ("buildings", "rocks", "trees")

SOURCE_COLLECTIONS = (LANDSCAPE, BUILDINGS, ROCKS) + LIBRARY

# --- Recentre ---------------------------------------------------------------

# The art was kitbashed ~158 m from the origin. Nothing depends on that offset,
# and everything downstream is easier without it: the collision grid in
# tools/make_forest_level.py is built centred on the origin, and the shadow
# cascades are framed off Level::bounds.
#
# Derived from the `ground` object's plan-view centre, which was
# (-146.9, -16.1) when this was written. Hardcoded rather than recomputed so
# that editing the terrain cannot silently shift the whole map out from under
# the collision proxies; run with --report and the script re-derives it and
# complains if it has drifted.
RECENTRE = Vector((146.9, 16.1, 0.0))
RECENTRE_TOLERANCE = 1.0

# --- Scale ------------------------------------------------------------------

# How much larger the level ships than it was kitbashed.
#
# The art was modelled at roughly half human scale relative to Player's 1.8 m:
# the compound wall stood 2.26 m, barely taller than the character, and the keep
# 9.13 m. At 3x they become 6.8 m and 27.4 m, which is what a castle is, and the
# island grows from 80 m across to 240 m.
#
# Applied here, at generation time, to everything this script emits -- never to
# the authored art. That is what keeps a re-run idempotent: the source stays at
# its original size and is scaled once on the way out, rather than being scaled
# again on top of an already-scaled result.
#
# Everything below is authored in the original units, so the constants can still
# be read against what the .blend shows. Only the final placement scales.
#
# What does NOT scale, because it describes the player rather than the world:
# MAX_STEP, BODY_HEIGHT, WALK_PROBE and WALK_MAX_RISE. The character is the same
# size in a bigger world -- which is the entire point -- so the walkability check
# has to run in final units with player-sized constants.
#
# Affordable now, and it would not have been before: terrain collision is a mesh,
# and a mesh scales for free. The box staircase it replaced needed cells fixed at
# ~3 m to keep neighbour steps under MAX_STEP, so tripling the world would have
# meant nine times the cells -- roughly 3,000 ground proxies in a linear scan.
WORLD_SCALE = 3.0

# --- Terrain ----------------------------------------------------------------

# `ground` is a closed solid: 73,106 quads, of which 25,939 face straight down.
# It is an island sitting in a moat, so its underside is below the water and
# below the far side of every sightline the camera has. Dropping those faces is
# exact -- no vertex moves -- and it is about a third of the object.
#
# The threshold is deliberately strict. A landscape's true underside points at
# -1.0; anything shallower than this could be the inside of an overhanging
# cliff, which is visible from below it.
UNDERSIDE_NORMAL_Z = -0.7

# Target triangle counts after decimation. Collapse rather than un-subdivide:
# the source is a uniform 0.5 m grid, so un-subdivide would halve resolution
# everywhere equally, while collapse keeps vertices where the terrain actually
# bends and strips them off the flats. Terrain is organic, so the rounding
# collapse does to hard edges is not a problem here (it is for the buildings,
# which use planar decimation instead).
GROUND_TARGET_TRIS = 10000

# `mountains` is deliberately not decimated, only stripped of its underside.
# It is a 153 m backdrop ring: dropping it from ~6,900 triangles to 5,000 saves
# under 2,000 triangles on a mesh that is drawn unconditionally anyway, and it
# costs a lot of shape -- at that density a single face spans tens of metres,
# so the skyline visibly facets and the cliffs flatten out. Cheap triangles in
# the right place are not the thing to economise on.
MOUNTAIN_TARGET_TRIS = None

# `water` is a 145 x 145 m slab carrying 10,206 quads for 2.3 m of relief. It
# is replaced outright by a flat grid at its top surface.
WATER_GRID = 8

# The backdrop is clipped to this radius about the island centre.
#
# Not a triangle budget -- a shadow one. export_level.py sets Level::bounds
# from everything drawn, and ShadowMap frames its far cascade on those bounds
# in light space, so what it has to cover is the diagonal. Left at their
# authored size the moat and mountain ring give a 217 m diagonal, past the
# 200 m a 2048-texel cascade can reach, and the engine says so at load:
# "distant shadows will be missing at the rim". 68 m brings the diagonal to
# ~192 m. The island is 40 m across and the water starts falling away well
# before this, so the clip lands on backdrop the player only ever sees edge-on.
BACKDROP_RADIUS = 68.0

# --- Buildings --------------------------------------------------------------

# Planar decimation merges coplanar faces and leaves hard edges alone, which is
# what architecture needs -- collapse would round the crenellations off. The
# angle is the most a face normal may turn and still be merged.
BUILDING_PLANAR_ANGLE = math.radians(4.0)

# Only worth doing on the heavy pieces; below this a decimate modifier costs
# more in evaluation time than it saves in triangles.
BUILDING_DECIMATE_MIN_TRIS = 3000

# --- Scatter ----------------------------------------------------------------

# Seven hair particle systems carry 2,650 instances. Blender's glTF exporter
# ignores particle instances entirely, so exporting without realising them
# first loses every tree and scattered rock with no error at all -- the level
# just arrives bald. They are realised here by reading the depsgraph directly
# rather than through duplicates_make_real, so each instance can be culled as
# it is read.
GROUND_EMITTER = "ground"
MOUNTAIN_EMITTER = "mountains"

# Island half-extent is ~40 m. Trees past this are on the outer slope, below
# the boundary ring the player is stopped by.
SCATTER_PLAY_RADIUS = 38.0

# Trees inside the compound grow through the walls. Footprint in original
# Blender world XY, expanded by the margin.
COMPOUND_MIN_XY = (-166.3, -28.7)
COMPOUND_MAX_XY = (-127.1, -3.0)
COMPOUND_MARGIN = 2.0

# The bridge corridor, likewise, in original world XY.
BRIDGE_MIN_XY = (-141.0, -20.0)
BRIDGE_MAX_XY = (-126.0, -12.5)

# How many survivors to keep. Subsampling is deterministic (sort by position,
# take every Nth) so that a re-run produces the same forest, not a new one.
GROUND_TREE_TARGET = 300
GROUND_ROCK_TARGET = 120

# The 1,000 trees on `mountains` are backdrop on a ring the player can never
# reach. Only the band facing the island reads on screen at all.
MOUNTAIN_BAND = (42.0, 72.0)
MOUNTAIN_TREE_TARGET = 180

# --- Chunking ---------------------------------------------------------------

# Side of the spatial cell that VISUAL is merged within, in metres. This is the
# whole draw-call/culling trade-off in one number: it is the granularity at
# which anything can be culled, and roughly the size of the AABB every merged
# mesh ends up with.
#
# Expressed in pre-scale units like everything else here, so the shipped cell is
# CHUNK_SIZE * WORLD_SCALE = 36 m. Left at the 24 that suited the unscaled map
# it would have become 72 m cells on a 240 m island -- three cells across the
# whole playable area, which is no culling at all.
CHUNK_SIZE = 12.0

# --- Collision --------------------------------------------------------------

# The ground mesh runs out at about 41 m from the island centre; past 39 the
# radial sampling starts missing it.
PLAY_RADIUS = 39.0

MAX_STEP = 0.3              # PhysicsManager.cpp:176

# The ground, the buildings, the bridge and the placed rocks are all carried by
# COLLISION_MESH now, and with them went every constant that existed to make a
# box staircase behave: the cell size and sampling rate, the height quantum and
# step relaxation, the cliff threshold that decided which slopes to give up on,
# the gate doorway carving, and the bridge threshold steps. None of them
# described the world -- they described how badly boxes could approximate it.
#
# What is left is what genuinely still wants to be a primitive.

# Boundary ring at the outer edge of the walkable island. Stays a proxy because
# it is an invisible wall: there is no geometry for it to be part of.
BOUNDARY_RADIUS = 37.0
BOUNDARY_SEGMENTS = 32
BOUNDARY_HEIGHT = 6.0
BOUNDARY_THICKNESS = 2.0

# The bridge's centreline, still used to place spawns and walkability landmarks.
GATE_AXIS_Y = -16.42

# Trees are collided as their trunk, measured off the bottom of each instance.
# The canopy is deliberately excluded: wrapping a conifer's skirt would turn a
# walkable forest into a maze of invisible cylinders.
TRUNK_SLICE = 1.5
TRUNK_MIN_HALF = 0.35
TRUNK_MAX_HALF = 1.2

# Spawns, in original Blender world XY. The player arrives at the east end of
# the bridge, looking down it at the gate.
PLAYER_SPAWN_XY = (-124.0, -16.4)
PLAYER_FACES_XY = (-141.2, -16.4)

# (suffix, x, y) -- the ambush: two flanking the bridge head on the near side,
# two waiting inside the gate. All four are on plateau; the obvious-looking
# positions out over the bridge itself are not, because the bridge deck is a
# building proxy and the terrain under it is the ravine floor 4 m below, which
# is where a marker placed there would actually land.
ENEMY_SPAWNS_XY = [
    ("01", -125.5, -13.5),
    ("02", -125.5, -19.5),
    ("03", -144.5, -13.5),
    ("04", -145.5, -19.5),
]

# Viewport display colours, which is what the in-game debug overlay draws each
# proxy in. Colour-coded by role so the overlay is readable.
COLOR_GROUND = (0.32, 0.42, 0.26, 1.0)
COLOR_BUILDING = (0.55, 0.55, 0.58, 1.0)
COLOR_BRIDGE = (0.72, 0.62, 0.36, 1.0)
COLOR_ROCK = (0.45, 0.45, 0.48, 1.0)
COLOR_TREE = (0.42, 0.26, 0.14, 1.0)
COLOR_BOUNDARY = (0.70, 0.13, 0.13, 1.0)


# ---------------------------------------------------------------------------
# Blender helpers
# ---------------------------------------------------------------------------

def get_collection(name):
    """Fetch or create a collection linked to the scene root."""
    existing = bpy.data.collections.get(name)
    if existing is None:
        existing = bpy.data.collections.new(name)
    if existing.name not in bpy.context.scene.collection.children:
        bpy.context.scene.collection.children.link(existing)
    return existing


def clear_collection(collection):
    """Remove every object in the collection, and the meshes they owned.

    The meshes have to go too. These are generated each run and nothing else
    references them, so leaving them behind would grow the .blend by the size
    of a whole VISUAL set on every re-run.
    """
    meshes = set()
    for obj in list(collection.all_objects):
        if obj.type == "MESH" and obj.data is not None:
            meshes.add(obj.data)
        bpy.data.objects.remove(obj, do_unlink=True)
    for mesh in meshes:
        if mesh.users == 0:
            bpy.data.meshes.remove(mesh)


def source_collection(name):
    collection = bpy.data.collections.get(name)
    if collection is None:
        raise SystemExit(
            "the .blend has no collection named %r. This script expects the "
            "art as authored: %s." % (name, ", ".join(SOURCE_COLLECTIONS)))
    return collection


def tri_count(mesh):
    return sum(max(0, len(p.vertices) - 2) for p in mesh.polygons)


def baked_mesh(obj, extra=()):
    """The object's mesh with its modifiers applied, plus `extra` on top.

    `extra` is a list of (setup_fn) callables that add a modifier to a
    temporary copy. The copy exists only so the authored object never gets a
    modifier stack this script put there -- see the note in the docstring about
    why nothing here edits the art.
    """
    if not extra:
        depsgraph = bpy.context.evaluated_depsgraph_get()
        return bpy.data.meshes.new_from_object(
            obj.evaluated_get(depsgraph), depsgraph=depsgraph)

    temp = obj.copy()
    temp.data = obj.data.copy()
    bpy.context.scene.collection.objects.link(temp)
    try:
        for setup in extra:
            setup(temp)
        depsgraph = bpy.context.evaluated_depsgraph_get()
        depsgraph.update()
        mesh = bpy.data.meshes.new_from_object(
            temp.evaluated_get(depsgraph), depsgraph=depsgraph)
    finally:
        data = temp.data
        bpy.data.objects.remove(temp, do_unlink=True)
        if data.users == 0:
            bpy.data.meshes.remove(data)
    return mesh


def drop_loose_verts(bm):
    """Remove vertices no face uses any more.

    bmesh's FACES delete context removes the faces and nothing else, so the
    vertices they used stay in the mesh as loose geometry. They are invisible
    and cost nothing to draw, which is exactly why this is easy to miss -- but
    they still count towards the mesh's bounding box, and the bounding box is
    what every cull test in the renderer is made against. A chunk of terrain
    that kept a copy of the whole island's vertices gets the whole island's
    AABB, passes every frustum test, and is drawn in full forever.
    """
    loose = [v for v in bm.verts if not v.link_faces]
    if loose:
        bmesh.ops.delete(bm, geom=loose, context="VERTS")
    return len(loose)


def drop_underside(mesh, matrix, threshold=UNDERSIDE_NORMAL_Z):
    """Delete faces whose world normal points straight down. In place."""
    normal_matrix = matrix.to_3x3().inverted().transposed()
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bm.normal_update()
    doomed = [f for f in bm.faces
              if (normal_matrix @ f.normal).normalized().z < threshold]
    removed = len(doomed)
    if doomed:
        bmesh.ops.delete(bm, geom=doomed, context="FACES")
        drop_loose_verts(bm)
    bm.to_mesh(mesh)
    bm.free()
    return removed


def clip_to_radius(mesh, matrix, centre, radius):
    """Delete faces whose centroid is further than `radius` from `centre`."""
    bm = bmesh.new()
    bm.from_mesh(mesh)
    doomed = []
    for face in bm.faces:
        world = matrix @ face.calc_center_median()
        if math.hypot(world.x - centre[0], world.y - centre[1]) > radius:
            doomed.append(face)
    removed = len(doomed)
    if doomed:
        bmesh.ops.delete(bm, geom=doomed, context="FACES")
        drop_loose_verts(bm)
    bm.to_mesh(mesh)
    bm.free()
    return removed


def decimate_setup(ratio=None, planar=None):
    def setup(obj):
        mod = obj.modifiers.new("__decimate", "DECIMATE")
        if planar is not None:
            mod.decimate_type = "DISSOLVE"
            mod.angle_limit = planar
        else:
            mod.decimate_type = "COLLAPSE"
            mod.ratio = ratio
    return setup


def make_object(name, mesh, collection, matrix=None):
    obj = bpy.data.objects.new(name, mesh)
    if matrix is not None:
        obj.matrix_world = matrix
    collection.objects.link(obj)
    return obj


# ---------------------------------------------------------------------------
# Merging
# ---------------------------------------------------------------------------

def merge_entries(name, entries):
    """Fuse (mesh, matrix) pairs into one mesh, remapping material slots.

    Done with bmesh rather than bpy.ops.object.join because join depends on
    selection and active-object context, which is exactly the kind of thing
    that behaves differently under --background. This also lets the material
    list be built deliberately: each source mesh's slots are mapped onto a
    shared list, so the merged mesh carries one slot per distinct material
    rather than one per source object.

    The slot count matters downstream -- glTF splits a mesh into one primitive
    per material, and raylib turns each primitive into one mesh and one
    DrawMesh. Merging twenty wall pieces that share three materials is a
    twenty-to-three win, not a twenty-to-one.
    """
    merged = bpy.data.meshes.new(name)
    materials = []
    index_of = {}

    bm = bmesh.new()
    for mesh, matrix in entries:
        remap = []
        for slot in (mesh.materials if mesh.materials else [None]):
            key = slot.name if slot is not None else None
            if key not in index_of:
                index_of[key] = len(materials)
                materials.append(slot)
            remap.append(index_of[key])

        piece = bmesh.new()
        piece.from_mesh(mesh)
        bmesh.ops.transform(piece, matrix=matrix, verts=piece.verts)
        for face in piece.faces:
            face.material_index = remap[min(face.material_index,
                                            len(remap) - 1)]
        temp = bpy.data.meshes.new("__piece")
        piece.to_mesh(temp)
        piece.free()
        bm.from_mesh(temp)
        bpy.data.meshes.remove(temp)

    bm.to_mesh(merged)
    bm.free()

    for material in materials:
        merged.materials.append(material)
    return merged


def chunk_key(point, size=CHUNK_SIZE):
    return (int(math.floor(point.x / size)), int(math.floor(point.y / size)))


def split_into_chunks(mesh, matrix, size=CHUNK_SIZE):
    """Cut a mesh into per-cell meshes. Returns [(mesh, matrix)].

    Applied to everything, not just the terrain, because the thing that has to
    be bounded is the *span* of a mesh rather than its triangle count. The
    water is 128 triangles across 145 m; merged whole into a cell it would drag
    that cell's AABB out to cover the entire map and take every triangle
    merged alongside it out of the reach of the cull.

    Faces are assigned by centroid, so a face straddling a cell boundary lands
    in exactly one cell -- no geometry is duplicated and none is lost.
    """
    buckets = defaultdict(list)
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bm.faces.ensure_lookup_table()
    for face in bm.faces:
        buckets[chunk_key(matrix @ face.calc_center_median(), size)].append(
            face.index)
    bm.free()

    # Already inside one cell: hand back the mesh itself rather than paying for
    # a full copy. This is the common case -- a rock or a wall piece.
    if len(buckets) <= 1:
        key = next(iter(buckets)) if buckets else (0, 0)
        return [(key, mesh, matrix)]

    pieces = []
    for key, indices in sorted(buckets.items()):
        part = bmesh.new()
        part.from_mesh(mesh)
        part.faces.ensure_lookup_table()
        keep = set(indices)
        doomed = [f for f in part.faces if f.index not in keep]
        if doomed:
            bmesh.ops.delete(part, geom=doomed, context="FACES")
        drop_loose_verts(part)
        out = bpy.data.meshes.new("__chunk_%d_%d" % key)
        part.to_mesh(out)
        part.free()
        for material in mesh.materials:
            out.materials.append(material)
        if out.polygons:
            pieces.append((key, out, matrix.copy()))
        else:
            bpy.data.meshes.remove(out)
    return pieces


# ---------------------------------------------------------------------------
# Scatter
# ---------------------------------------------------------------------------

def in_box(point, lo, hi, margin=0.0):
    return (lo[0] - margin <= point.x <= hi[0] + margin
            and lo[1] - margin <= point.y <= hi[1] + margin)


def read_instances(emitter_name):
    """Every particle instance the emitter produces, as (source, matrix).

    Read off the depsgraph rather than realised with duplicates_make_real: the
    operator would create 1,650 objects and leave culling until afterwards,
    whereas this rejects them before they ever become objects.
    """
    depsgraph = bpy.context.evaluated_depsgraph_get()
    found = []
    for instance in depsgraph.object_instances:
        if not instance.is_instance or instance.parent is None:
            continue
        if instance.parent.original.name != emitter_name:
            continue
        source = instance.object.original
        if source.type != "MESH" or source.data is None:
            continue
        found.append((source, instance.matrix_world.copy()))
    return found


def subsample(items, target):
    """Keep `target` of `items`, evenly and repeatably.

    Sorted by position first: the depsgraph's instance order is not something
    to rely on staying stable across Blender versions, and a forest that
    reshuffles itself on every re-run would make every diff of the exported
    level meaningless.
    """
    items = sorted(items, key=lambda pair: (round(pair[1].translation.x, 3),
                                            round(pair[1].translation.y, 3)))
    if len(items) <= target or target <= 0:
        return items
    step = len(items) / float(target)
    return [items[int(i * step)] for i in range(target)]


def gather_ground_scatter(island_centre):
    trees, rocks = [], []
    for source, matrix in read_instances(GROUND_EMITTER):
        position = matrix.translation
        offset = Vector((position.x - island_centre[0],
                         position.y - island_centre[1]))
        if offset.length > SCATTER_PLAY_RADIUS:
            continue
        if in_box(position, COMPOUND_MIN_XY, COMPOUND_MAX_XY, COMPOUND_MARGIN):
            continue
        if in_box(position, BRIDGE_MIN_XY, BRIDGE_MAX_XY):
            continue
        (trees if source.name.startswith("tree") else rocks).append(
            (source, matrix))
    return (subsample(trees, GROUND_TREE_TARGET),
            subsample(rocks, GROUND_ROCK_TARGET))


def gather_mountain_scatter(island_centre):
    kept = []
    for source, matrix in read_instances(MOUNTAIN_EMITTER):
        position = matrix.translation
        distance = math.hypot(position.x - island_centre[0],
                              position.y - island_centre[1])
        if not MOUNTAIN_BAND[0] <= distance <= MOUNTAIN_BAND[1]:
            continue
        kept.append((source, matrix))
    return subsample(kept, MOUNTAIN_TREE_TARGET)


# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

def island_centre_of(ground):
    corners = [ground.matrix_world @ Vector(c) for c in ground.bound_box]
    xs = [c.x for c in corners]
    ys = [c.y for c in corners]
    return ((min(xs) + max(xs)) * 0.5, (min(ys) + max(ys)) * 0.5)


def check_recentre(island_centre):
    derived = Vector((-island_centre[0], -island_centre[1], 0.0))
    drift = (derived - RECENTRE).length
    if drift > RECENTRE_TOLERANCE:
        raise SystemExit(
            "RECENTRE is %s but the island now centres on %s (drift %.2f m). "
            "The terrain has moved since this constant was set. Update "
            "RECENTRE and rebuild collision -- do not leave them disagreeing, "
            "or the proxies end up %.1f m from the geometry they wrap."
            % (tuple(round(c, 2) for c in RECENTRE),
               tuple(round(-c, 2) for c in derived[:2]), drift, drift))
    return derived


def build_terrain(report, centre):
    """The landscape, as (chunked entries, backdrop entries).

    The split is a judgement about what is worth culling. `ground` is the
    playable island and chunks cleanly. `water` and `mountains` are the moat
    and the ring around it: both are visible from very nearly everywhere the
    camera can be, and both are wider than any cell, so chunking them would buy
    a few thousand triangles at the cost of dozens of draw calls. They are kept
    whole and drawn unconditionally instead -- which is a deliberate choice
    here rather than the accident it would be if they were merged in with
    geometry that *can* be culled.
    """
    entries = []
    backdrop = []

    ground = bpy.data.objects["ground"]
    mesh = baked_mesh(ground)
    before = tri_count(mesh)
    dropped = drop_underside(mesh, ground.matrix_world)
    after_cull = tri_count(mesh)
    ratio = min(1.0, GROUND_TARGET_TRIS / float(max(1, after_cull)))
    bpy.data.meshes.remove(mesh)

    mesh = baked_mesh(ground)
    drop_underside(mesh, ground.matrix_world)
    temp = make_object("__ground_tmp", mesh, bpy.context.scene.collection,
                       ground.matrix_world)
    final = baked_mesh(temp, [decimate_setup(ratio=ratio)])
    bpy.data.objects.remove(temp, do_unlink=True)
    bpy.data.meshes.remove(mesh)
    report["ground"] = (before, dropped, after_cull, tri_count(final))
    entries.append((final, ground.matrix_world.copy()))

    water = bpy.data.objects["water"]
    mesh = baked_mesh(water)
    report["water"] = (tri_count(mesh), None, None, None)
    flat = flat_water(mesh)
    bpy.data.meshes.remove(mesh)
    clip_to_radius(flat, water.matrix_world, centre, BACKDROP_RADIUS)
    report["water"] = (report["water"][0], 0, 0, tri_count(flat))
    backdrop.append((flat, water.matrix_world.copy()))

    mountains = bpy.data.objects["mountains"]
    final = baked_mesh(mountains)
    before = tri_count(final)
    dropped = drop_underside(final, mountains.matrix_world)
    clip_to_radius(final, mountains.matrix_world, centre, BACKDROP_RADIUS)
    after_cull = tri_count(final)
    if MOUNTAIN_TARGET_TRIS is not None and after_cull > MOUNTAIN_TARGET_TRIS:
        ratio = MOUNTAIN_TARGET_TRIS / float(after_cull)
        temp = make_object("__mtn_tmp", final, bpy.context.scene.collection,
                           mountains.matrix_world)
        decimated = baked_mesh(temp, [decimate_setup(ratio=ratio)])
        bpy.data.objects.remove(temp, do_unlink=True)
        bpy.data.meshes.remove(final)
        final = decimated
    report["mountains"] = (before, dropped, after_cull, tri_count(final))
    backdrop.append((final, mountains.matrix_world.copy()))

    for curve in ("path1", "path2"):
        obj = bpy.data.objects.get(curve)
        if obj is None:
            continue
        mesh = baked_mesh(obj)
        if mesh.polygons:
            entries.append((mesh, obj.matrix_world.copy()))
        else:
            bpy.data.meshes.remove(mesh)

    return entries, backdrop


def flat_water(source):
    """A coarse grid at the slab's top surface, in the object's local space."""
    top = max((v.co.z for v in source.vertices), default=0.0)
    xs = [v.co.x for v in source.vertices]
    ys = [v.co.y for v in source.vertices]
    lo = (min(xs), min(ys))
    hi = (max(xs), max(ys))

    mesh = bpy.data.meshes.new("water_flat")
    verts, faces = [], []
    n = WATER_GRID
    for j in range(n + 1):
        for i in range(n + 1):
            verts.append((lo[0] + (hi[0] - lo[0]) * i / n,
                          lo[1] + (hi[1] - lo[1]) * j / n, top))
    for j in range(n):
        for i in range(n):
            a = j * (n + 1) + i
            faces.append((a, a + 1, a + n + 2, a + n + 1))
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    if source.materials:
        mesh.materials.append(source.materials[0])
    return mesh


def build_props(report):
    """Buildings and rocks, as (mesh, matrix) entries."""
    entries = []
    decimated = 0

    for collection_name in (BUILDINGS, ROCKS):
        for obj in sorted(source_collection(collection_name).objects,
                          key=lambda o: o.name):
            if obj.type != "MESH":
                continue
            mesh = baked_mesh(obj)
            before = tri_count(mesh)
            if before >= BUILDING_DECIMATE_MIN_TRIS:
                bpy.data.meshes.remove(mesh)
                mesh = baked_mesh(
                    obj, [decimate_setup(planar=BUILDING_PLANAR_ANGLE)])
                decimated += 1
            report["props_before"] += before
            report["props_after"] += tri_count(mesh)
            entries.append((mesh, obj.matrix_world.copy()))

    report["props_decimated"] = decimated
    return entries


def build_scatter(report, island_centre):
    entries = []
    trees, rocks = gather_ground_scatter(island_centre)
    mountain = gather_mountain_scatter(island_centre)

    if not trees and not rocks and not mountain:
        raise SystemExit(
            "the particle emitters produced no instances at all. The scatter "
            "is 2,650 instances of the seven hair systems on 'ground' and "
            "'mountains'; finding none of them means they are not being "
            "evaluated -- check that the source collections are visible, "
            "since hide_viewport takes a collection out of the depsgraph. "
            "Exporting from here would produce a level with no trees and no "
            "error to say so.")

    for source, matrix in trees + rocks + mountain:
        mesh = baked_mesh(source)
        report["scatter_after"] += tri_count(mesh)
        entries.append((mesh, matrix))

    report["scatter_kept"] = (len(trees), len(rocks), len(mountain))
    # The island trees are handed back so their trunks can be collided. The
    # backdrop ones are not: they are on a mountain the player cannot reach.
    return entries, trees


BACKDROP_NAME = "BACKDROP"


def build_collision_mesh(entries, collection, offset):
    """Merge `entries` into the single mesh the engine collides against.

    What goes in is a judgement about what should be solid, and it is not the
    same set VISUAL draws:

      * ground and the paths on it -- the whole point of the exercise. The
        terrain is a curved surface and now collides as one, instead of as a
        341-box staircase quantised to 0.25 m that wrote off 386 neighbour
        pairs as unclimbable cliffs.
      * buildings, the bridge and the placed rocks -- the walls, the arches and
        the round tower bases, at the resolution they are drawn at.

    and what stays out:

      * water and the mountain ring, which are backdrop the player is kept away
        from by the boundary proxies; meshing them would add 7k triangles of
        surface nobody can stand on.
      * the scattered trees. Their trunks are cheap BOX_ proxies, and they
        should be: a tree is ~104 triangles of which nearly all are canopy, and
        colliding against a conifer's skirt turns a walkable forest into a maze.

    Merged into one object rather than left as many, because it is loaded as a
    single triangle soup and indexed by one BVH -- object boundaries carry no
    meaning past this point.
    """
    merged = merge_entries(COLLISION_MESH, entries)
    obj = make_object(COLLISION_MESH, merged, collection)
    # The merged vertices are in the original frame, so the object carries both
    # the recentre and the scale: world = (v + offset) * WORLD_SCALE.
    obj.scale = (WORLD_SCALE, WORLD_SCALE, WORLD_SCALE)
    obj.location = Vector(offset) * WORLD_SCALE
    return obj


def assemble(entries, backdrop, visual, offset):
    """Merge entries into per-chunk objects inside VISUAL.

    Everything is put through split_into_chunks first, so a cell's merged mesh
    can only ever be about one cell across. Bucketing by an entry's centroid
    instead would look like it worked and quietly wouldn't: a mesh wider than a
    cell lands in one bucket and inflates that bucket's bounding box to its own
    full span, which is how a level ends up with one uncullable chunk holding a
    third of its triangles.
    """
    buckets = defaultdict(list)
    intermediate = []
    for mesh, matrix in entries:
        for key, piece, piece_matrix in split_into_chunks(mesh, matrix):
            buckets[key].append((piece, piece_matrix))
            if piece is not mesh:
                intermediate.append(piece)

    shift = Vector(offset)
    made = 0
    for key, group in sorted(buckets.items()):
        name = "CHUNK_%+04d_%+04d" % key
        merged = merge_entries(name, group)
        obj = make_object(name, merged, visual)
        obj.scale = (WORLD_SCALE, WORLD_SCALE, WORLD_SCALE)
        obj.location = shift * WORLD_SCALE
        made += 1

    if backdrop:
        obj = make_object(BACKDROP_NAME,
                          merge_entries(BACKDROP_NAME, backdrop), visual)
        obj.scale = (WORLD_SCALE, WORLD_SCALE, WORLD_SCALE)
        obj.location = shift * WORLD_SCALE
        made += 1

    for mesh in intermediate:
        if mesh.users == 0:
            bpy.data.meshes.remove(mesh)
    for mesh, _ in list(entries) + list(backdrop):
        if mesh.users == 0:
            bpy.data.meshes.remove(mesh)
    return made


def set_sources_hidden(hidden):
    """Show or hide the authored art.

    Hiding it at the end is cosmetic: VISUAL holds an optimized copy sitting in
    the same place, so leaving both on would z-fight and make it impossible to
    see what the level actually looks like.

    Showing it at the *start* is not cosmetic at all, and is the reason this is
    one function rather than a hide_sources(). A collection's hide_viewport
    takes it out of depsgraph evaluation, so on a second run the sources would
    still be hidden from the previous one: modifiers would stop being applied
    and read_instances would find no particle instances. The level would export
    with its mirrored geometry halved and every tree gone, and nothing would
    report an error -- it would just quietly build a worse level than the run
    before. Re-running has to mean re-running from the same starting state.
    """
    for name in SOURCE_COLLECTIONS:
        collection = bpy.data.collections.get(name)
        if collection is not None:
            collection.hide_viewport = hidden
    # Nothing below reads the depsgraph until this has taken effect.
    bpy.context.view_layer.update()
def build_tree_proxies(collision, trees):
    """Trunk boxes for the realised scatter, measured in a slice above the base."""
    made = 0
    for index, (source, matrix) in enumerate(trees):
        points = [matrix @ v.co for v in source.data.vertices]
        if not points:
            continue
        base = min(p.z for p in points)
        band = [p for p in points if p.z <= base + TRUNK_SLICE]
        if not band:
            continue
        half_x = min(TRUNK_MAX_HALF, max(
            TRUNK_MIN_HALF,
            (max(p.x for p in band) - min(p.x for p in band)) * 0.5))
        half_y = min(TRUNK_MAX_HALF, max(
            TRUNK_MIN_HALF,
            (max(p.y for p in band) - min(p.y for p in band)) * 0.5))
        top = max(p.z for p in points)
        cx = sum(p.x for p in band) / len(band)
        cy = sum(p.y for p in band) / len(band)
        add_box(collision, "BOX_Tree_%03d" % index,
                centre=(cx, cy, (base + top) * 0.5),
                size=(half_x * 2.0, half_y * 2.0, top - base),
                yaw_deg=0.0, colour=COLOR_TREE)
        made += 1
    return {"boxes": made}


def build_boundary(collision, terrain, centre):
    """A closed ring of yawed boxes at the edge of the ground proxies.

    Without it the player walks off the last ground box and falls: the island's
    outer slope has no collision at all, by design.
    """
    for index in range(BOUNDARY_SEGMENTS):
        angle = math.tau * index / BOUNDARY_SEGMENTS
        cx = centre[0] + math.cos(angle) * BOUNDARY_RADIUS
        cy = centre[1] + math.sin(angle) * BOUNDARY_RADIUS
        ground = terrain_height(terrain, cx, cy)
        if ground is None:
            ground = 0.0
        # Overlapped by 15% so the corners between segments cannot open a gap.
        span = math.tau * BOUNDARY_RADIUS / BOUNDARY_SEGMENTS * 1.15
        add_box(collision, "BOX_Boundary_%02d" % index,
                centre=(cx, cy, ground + BOUNDARY_HEIGHT * 0.5 - 1.0),
                size=(BOUNDARY_THICKNESS, span, BOUNDARY_HEIGHT),
                yaw_deg=math.degrees(angle), colour=COLOR_BOUNDARY)
    return {"boxes": BOUNDARY_SEGMENTS}


def build_markers(markers, terrain):
    x, y = PLAYER_SPAWN_XY
    z = terrain_height(terrain, x, y)
    if z is None:
        raise SystemExit("player spawn (%.1f, %.1f) is off the terrain"
                         % (x, y))
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


WALK_PROBE = 0.5            # metres between walkability samples
BODY_HEIGHT = 1.8           # Player::BODY_HEIGHT

# cos(60 deg) -- PhysicsManager's COS_MAX_SLOPE. A surface tilted further than
# this is not standable, whatever its height.
COS_MAX_SLOPE = 0.5

# The most two neighbouring probes may differ in height and still be considered
# connected.
#
# NOT MAX_STEP, and that distinction is the whole difference between grading a
# staircase and grading a surface. MAX_STEP is the engine's rule for a
# *discontinuity* -- how high a ledge the character can step up. It is applied
# per frame, over the centimetre or two of movement in one tick, where even a
# 45-degree slope rises far less than it. Applying it between probes half a
# metre apart instead asks "is this slope flat", and answers no for every real
# hillside, which is how a continuous island came out as 729 disconnected
# islands.
#
# So neighbours are joined when the grade between them is walkable, and blocked
# when it is not. tan(45 deg) * WALK_PROBE keeps genuine cliffs -- the ravine
# drops several metres in half a metre -- firmly apart.
WALK_MAX_RISE = 0.5


def check_walkability(mesh_obj, collision, start_xy, start_z, landmarks):
    """Flood-fill outward from the spawn the way the character actually moves.

    Reachability is grown from the spawn rather than sampled globally, and the
    probe for each new cell starts just above the height of the cell it is
    entered from -- which is what PhysicsManager does every frame
    (probeMeshGround casts down from pos.y + MAX_STEP).

    Sampling from high above instead looks equivalent and is not. A ray dropped
    from the sky onto a gatehouse hits its *roof*, so the floor under the arch
    reads as 3.5 m up, the ground either side reads as 0, and the archway you
    can plainly walk through comes out as a sealed 22 m2 pocket. That is exactly
    what this reported before the probe was moved down to the feet.

    A neighbour joins when it can be stepped up to (MAX_STEP) or walked down to
    (WALK_MAX_RISE), on a surface flat enough to stand on, with no proxy -- a
    tree trunk or the boundary ring -- occupying the body there.
    """
    blockers = []
    for obj in collision.all_objects:
        half = (obj.scale.x * 0.5, obj.scale.y * 0.5)
        yaw = obj.rotation_euler.z
        blockers.append((obj.location.x, obj.location.y, half[0], half[1],
                         math.cos(-yaw), math.sin(-yaw),
                         obj.location.z + obj.scale.z * 0.5,
                         obj.location.z - obj.scale.z * 0.5,
                         math.hypot(*half)))

    bucket_size = 8.0
    buckets = defaultdict(list)
    for entry in blockers:
        cx, cy, reach = entry[0], entry[1], entry[8]
        for bi in range(int((cx - reach) // bucket_size),
                        int((cx + reach) // bucket_size) + 1):
            for bj in range(int((cy - reach) // bucket_size),
                            int((cy + reach) // bucket_size) + 1):
                buckets[(bi, bj)].append(entry)

    def blocked(x, y, floor_z):
        low, high = floor_z + 0.05, floor_z + BODY_HEIGHT
        for entry in buckets.get((int(x // bucket_size), int(y // bucket_size)),
                                 ()):
            cx, cy, hx, hy, cos_y, sin_y = entry[:6]
            dx, dy = x - cx, y - cy
            if (entry[7] < high and entry[6] > low
                    and abs(dx * cos_y - dy * sin_y) <= hx
                    and abs(dx * sin_y + dy * cos_y) <= hy):
                return True
        return False

    inverse = mesh_obj.matrix_world.inverted()
    down = (inverse.to_3x3() @ Vector((0.0, 0.0, -1.0))).normalized()
    rotation = mesh_obj.matrix_world.to_3x3()

    # Object-space ray length. mesh_obj carries the WORLD_SCALE, and ray_cast
    # measures `distance` in the object's own space, so a world-metre budget
    # handed straight to it would reach WORLD_SCALE times too far and report
    # floors the player could never step down to.
    reach_local = (MAX_STEP + WALK_MAX_RISE) / WORLD_SCALE

    def floor_from(x, y, from_z):
        """Surface under (x, y) entered from `from_z`, or None. World units."""
        hit, location, normal, _ = mesh_obj.ray_cast(
            inverse @ Vector((x, y, from_z + MAX_STEP)), down,
            distance=reach_local)
        if not hit:
            return None
        if (rotation @ normal).normalized().z < COS_MAX_SLOPE:
            return None
        z = (mesh_obj.matrix_world @ location).z
        return None if blocked(x, y, z) else z

    # Final units: the world is WORLD_SCALE times bigger but the probe spacing
    # is not, because the character is not.
    extent = PLAY_RADIUS * WORLD_SCALE
    n = int(2.0 * extent / WALK_PROBE)
    origin = (-extent, -extent)

    def cell_of(x, y):
        return (int((y - origin[1]) / WALK_PROBE),
                int((x - origin[0]) / WALK_PROBE))

    def centre_of(j, i):
        return (origin[0] + (i + 0.5) * WALK_PROBE,
                origin[1] + (j + 0.5) * WALK_PROBE)

    reached = {}
    sj, si = cell_of(*start_xy)
    reached[(sj, si)] = start_z
    stack = [(sj, si)]
    while stack:
        j, i = stack.pop()
        here = reached[(j, i)]
        for dj, di in ((0, 1), (1, 0), (0, -1), (-1, 0)):
            nj, ni = j + dj, i + di
            if not (0 <= nj < n and 0 <= ni < n) or (nj, ni) in reached:
                continue
            x, y = centre_of(nj, ni)
            z = floor_from(x, y, here)
            if z is None:
                continue
            reached[(nj, ni)] = z
            stack.append((nj, ni))

    out = {}
    for name, point in landmarks.items():
        out[name] = cell_of(*point) in reached
    return out, len(reached) * WALK_PROBE * WALK_PROBE

def placed(xy):
    """An authored Blender XY moved into the frame the level ships in."""
    return ((xy[0] + RECENTRE.x) * WORLD_SCALE,
            (xy[1] + RECENTRE.y) * WORLD_SCALE)


def place_collection(collection, offset, scale):
    """Move generated proxies into the frame VISUAL and the mesh ship in.

    Everything above is authored against the terrain where it actually sits,
    which is 158 m from the origin and half the size it ships at, so the numbers
    in this file can be read against what the .blend shows. The proxies have to
    ride the identical recentre *and* scale or they end up wrapping geometry
    that is no longer there.

    A box's size is its object scale -- export_box reads bound_box and
    multiplies by it -- so scaling a proxy means scaling that too, not just its
    position. Markers are Empties with no extent and only move.
    """
    for obj in collection.all_objects:
        obj.location = (obj.location + Vector(offset)) * scale
        if obj.type == "MESH":
            obj.scale = obj.scale * scale


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------

def parse_args(argv):
    argv = argv[argv.index("--") + 1:] if "--" in argv else []
    parser = argparse.ArgumentParser(prog="make_castle_level.py")
    parser.add_argument("--out", default=None,
                        help="where to save (defaults to the input .blend)")
    parser.add_argument("--no-save", action="store_true",
                        help="build but do not write the .blend back")
    return parser.parse_args(argv)


def main():
    args = parse_args(sys.argv)

    for name in SOURCE_COLLECTIONS:
        source_collection(name)

    # Before anything reads the depsgraph: a previous run left these hidden.
    set_sources_hidden(False)

    ground = bpy.data.objects.get("ground")
    if ground is None:
        raise SystemExit("no object named 'ground' -- this is not the castle "
                         "approach .blend.")
    centre = island_centre_of(ground)
    check_recentre(centre)

    visual = get_collection(VISUAL)
    collision = get_collection(COLLISION)
    collision_mesh = get_collection(COLLISION_MESH)
    markers = get_collection(MARKERS)
    for generated in (visual, collision, collision_mesh, markers):
        clear_collection(generated)

    report = {"props_before": 0, "props_after": 0, "scatter_after": 0}

    entries, backdrop = build_terrain(report, centre)
    props = build_props(report)

    # The solid set is named here rather than filtered back out of `entries`
    # later: terrain and props are what the player stands on and walks into,
    # and the scatter that follows is explicitly not.
    solid = list(entries) + list(props)

    entries.extend(props)
    scatter, island_trees = build_scatter(report, centre)
    entries.extend(scatter)

    # Built before assemble(), which frees every entry mesh whose user count has
    # dropped to zero. merge_entries copies, so the collision object does not
    # keep those datablocks alive -- taking this after the merge would hand it
    # freed meshes.
    mesh_obj = build_collision_mesh(solid, collision_mesh, RECENTRE)
    report["collision_mesh_tris"] = tri_count(mesh_obj.data)

    raw_tris = sum(tri_count(mesh) for mesh, _ in entries + backdrop)
    raw_pieces = len(entries) + len(backdrop)
    meshes = assemble(entries, backdrop, visual, RECENTRE)

    # What is left in COLLISION after the mesh took over. Ground, buildings,
    # the bridge and the placed rocks are all real geometry now; the terrain
    # staircase, the gate carving and the bridge threshold steps that existed
    # only to work around boxes are gone with them.
    #
    # Trees stay proxies because a trunk is a box and a canopy should not
    # collide at all, and the boundary ring stays because it is an invisible
    # wall with no geometry to be.
    #
    # Built before the sources are hidden: a hidden collection is out of the
    # depsgraph, and terrain_height would be raycasting an object with no
    # evaluated mesh.
    proxies = {
        "trees": build_tree_proxies(collision, island_trees),
        "boundary": build_boundary(collision, ground, centre),
    }
    spawns = build_markers(markers, ground)

    place_collection(collision, RECENTRE, WORLD_SCALE)
    place_collection(markers, RECENTRE, WORLD_SCALE)

    # After the shift, so probes, proxies and the mesh are all in the frame the
    # level actually ships in. The island then centres on the origin.
    reach, walk_area = check_walkability(
        mesh_obj, collision, placed(PLAYER_SPAWN_XY),
        # The marker's own z, scaled the same way place_collection scaled it.
        # Seeding from the authored height instead puts the first probe a third
        # of the way up the terrain and the flood fill finds nothing at all.
        spawns["player"][2] * WORLD_SCALE, {
        "player spawn": placed(PLAYER_SPAWN_XY),
        "bridge middle": placed((-134.0, GATE_AXIS_Y)),
        "gate": placed((-141.2, GATE_AXIS_Y)),
        "courtyard": placed((-147.0, -16.0)),
        "keep approach": placed((-152.0, -16.0)),
    })

    set_sources_hidden(True)

    # COLLISION and MARKERS must stay visible, and not as a matter of taste.
    # export_level.py reads every proxy's matrix_world, and Blender only
    # computes matrix_world for objects the depsgraph evaluates -- a hidden
    # collection is not evaluated, so its objects keep an identity matrix and
    # the export writes the entire level's collision and both sets of spawns at
    # the origin. It does that silently: verify_level.py's bounds check is
    # dominated by the visual mesh, and its per-proxy check needs VIS_* meshes
    # this kitbashed map does not have, so nothing downstream notices.
    for generated in (collision, collision_mesh, markers):
        generated.hide_viewport = False

    total_tris = 0
    total_prims = 0
    widest = 0.0
    widest_name = ""
    uncullable = 0
    for obj in visual.objects:
        total_tris += tri_count(obj.data)
        total_prims += len({p.material_index for p in obj.data.polygons}) or 1
        if obj.name == BACKDROP_NAME:
            continue
        corners = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
        span = max(max(c[i] for c in corners) - min(c[i] for c in corners)
                   for i in (0, 1))
        if span > widest:
            widest, widest_name = span, obj.name
        # A chunk more than twice its cell across has swallowed something that
        # spans the map. It will pass every frustum test put to it. BACKDROP is
        # exempt because being always-drawn is what it is for.
        if span > CHUNK_SIZE * WORLD_SCALE * 2.0:
            uncullable += tri_count(obj.data)

    print("\n[make_castle_level] island centre %.1f, %.1f -> recentred by %s, "
          "scaled %.1fx" % (centre[0], centre[1],
                            tuple(round(c, 1) for c in RECENTRE), WORLD_SCALE))
    print("[make_castle_level] terrain")
    for key in ("ground", "water", "mountains"):
        before, dropped, culled, after = report[key]
        if dropped:
            print("    %-10s %7d -> %7d tris  (underside %d faces, then "
                  "decimate)" % (key, before, after, dropped))
        else:
            print("    %-10s %7d -> %7d tris" % (key, before, after))
    print("[make_castle_level] props     %7d -> %7d tris  (%d decimated)"
          % (report["props_before"], report["props_after"],
             report["props_decimated"]))
    trees, rocks, mountain = report["scatter_kept"]
    print("[make_castle_level] scatter   realised %d trees, %d rocks, %d "
          "backdrop = %d tris" % (trees, rocks, mountain,
                                  report["scatter_after"]))
    print("[make_castle_level] merge     %d pieces -> %d chunk meshes "
          "(%.0f m cells)" % (raw_pieces, meshes, CHUNK_SIZE))
    print("[make_castle_level] VISUAL    %d objects, %d glTF primitives, "
          "%d tris" % (len(visual.objects), total_prims, total_tris))
    backdrop_obj = visual.objects.get(BACKDROP_NAME)
    backdrop_tris = tri_count(backdrop_obj.data) if backdrop_obj else 0
    print("[make_castle_level] collmesh  %d tris (ground + paths + buildings "
          "+ placed rocks; water, mountains and the tree scatter excluded)"
          % report["collision_mesh_tris"])
    print("[make_castle_level] cull      widest cullable chunk %.1f m (%s); "
          "backdrop %d tris always drawn"
          % (widest, widest_name, backdrop_tris))
    if total_tris != raw_tris:
        print("[make_castle_level] WARNING merge changed the triangle count "
              "(%d -> %d)" % (raw_tris, total_tris))
    total_proxies = sum(p["boxes"] for p in proxies.values())
    print("[make_castle_level] collision %d proxies -- trees %d, boundary %d "
          "(ground, buildings, bridge and rocks are the mesh now)"
          % (total_proxies, proxies["trees"]["boxes"],
             proxies["boundary"]["boxes"]))
    print("[make_castle_level] walkable  %.0f m2 reachable from the spawn"
          % walk_area)
    for name, ok in reach.items():
        print("      %-14s %s" % (name, "reachable"
                                  if ok else "NOT REACHABLE FROM SPAWN"))
    print("[make_castle_level] spawns    %s" % (spawns,))

    if uncullable:
        # Worth failing loudly over. Triangles in a chunk this wide are drawn
        # every frame into the camera pass and every shadow cascade no
        # matter where the player is standing, which is the one outcome this
        # whole script exists to avoid.
        print("[make_castle_level] WARNING %d tris sit in chunks wider than "
              "%.0f m and will never be culled"
              % (uncullable, CHUNK_SIZE * WORLD_SCALE * 2))

    if not args.no_save:
        out = args.out or bpy.data.filepath
        bpy.ops.wm.save_as_mainfile(filepath=out)
        print("[make_castle_level] saved %s (%.1f MB)"
              % (out, os.path.getsize(out) / 1e6))


if __name__ == "__main__":
    main()
