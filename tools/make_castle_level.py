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
    add_box, add_marker, game_yaw, get_collection, measure_error,
    merge_rectangles, quantize_and_relax, report_steps, sample_ground_grid,
    terrain_height,
)

# --- Collections ------------------------------------------------------------

VISUAL = "VISUAL"
COLLISION = "COLLISION"
MARKERS = "MARKERS"

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
# mesh ends up with. 24 m against an 80 m island gives a 4x4 grid over the
# playable area, so the 16 m near shadow cascade still rejects most of it.
CHUNK_SIZE = 24.0

# --- Collision --------------------------------------------------------------

# The ground mesh runs out at about 41 m from the island centre; past 39 the
# radial sampling starts missing it.
PLAY_RADIUS = 39.0

GROUND_CELL = 3.0
GROUND_SAMPLES = 3

# Deep enough to span any cliff on the map (the terrain runs -8.7 to +4.6), so
# that a box at the top of the ravine wall still overlaps the box on the floor
# below it. A thin slab would leave a gap the player could walk into sideways
# underneath the plateau.
GROUND_THICKNESS = 10.0

MAX_STEP = 0.3              # PhysicsManager.cpp:176

# Coarser than the forest level's 0.05 m, and measured rather than guessed: the
# quantum is what lets neighbouring cells compare equal and merge into big
# rectangles, and this terrain is varied enough that a fine quantum merges
# almost nothing. At 0.05 m the grid gives 518 boxes; at 0.25 m it gives 340
# for 0.07 m more p95 error. Proxy count is not free -- PhysicsManager has no
# broadphase and scans the whole obstacle vector several times per character
# per frame (PhysicsManager.cpp:78, :120, :241, :360).
HEIGHT_QUANTUM = 0.25
STEP_QUANTA = 1             # 1 * 0.25 = 0.25 m, under MAX_STEP
RELAX_ITERATIONS = 600

# Above this a neighbour difference is the ravine, the moat wall or the outer
# slope, not a sampling artefact, and is left for the player to be stopped by.
# See the note on quantize_and_relax in tools/level_terrain.py -- relaxing
# these instead is what took the ground error from 0.35 m to 0.93 m.
CLIFF_QUANTA = 2            # 2 * 0.25 = 0.5 m

# Boundary ring at the outer edge of the ground proxies.
BOUNDARY_RADIUS = 37.0
BOUNDARY_SEGMENTS = 32
BOUNDARY_HEIGHT = 6.0
BOUNDARY_THICKNESS = 2.0

# The doorway carved through the gate so the bridge actually leads somewhere,
# in original Blender world coordinates. Centred on the bridge's own axis and
# and a little wider than its 1.96 m deck.
GATE_AXIS_Y = -16.42
GATE_WIDTH = 2.6
GATE_HEADROOM = 2.6         # Player::BODY_HEIGHT is 1.8

# Rocks below this footprint are scenery to step over rather than obstacles to
# walk around, and each one costs a linear scan in PhysicsManager.
ROCK_MIN_FOOTPRINT = 2.0

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
        obj.location = shift
        made += 1

    if backdrop:
        obj = make_object(BACKDROP_NAME,
                          merge_entries(BACKDROP_NAME, backdrop), visual)
        obj.location = shift
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


# ---------------------------------------------------------------------------
# Collision
# ---------------------------------------------------------------------------

def yaw_box_of(obj, depsgraph):
    """(centre, size, yaw) for a yaw-only box wrapping the evaluated object.

    Any pitch or roll on the object is deliberately dropped rather than
    reproduced. export_box rejects a proxy rotated about X or Y at all
    (export_level.py:131) because PhysicsObstacle has no such axis, and eleven
    of the compound's wall pieces are tilted between 1.5 and 4.3 degrees to sit
    on the slope. Over a 2.3 m wall that tilt moves the top corner by at most
    0.17 m, so a level box is a good stand-in -- whereas taking the object's
    world AABB instead would throw the yaw away too and turn a 1.6 x 3.1 m wall
    into a 3.4 x 2.3 m blob pointing the wrong way.
    """
    evaluated = obj.evaluated_get(depsgraph)
    corners = [Vector(c) for c in evaluated.bound_box]
    scale = obj.matrix_world.to_scale()
    size = Vector((
        (max(c.x for c in corners) - min(c.x for c in corners)) * abs(scale.x),
        (max(c.y for c in corners) - min(c.y for c in corners)) * abs(scale.y),
        (max(c.z for c in corners) - min(c.z for c in corners)) * abs(scale.z),
    ))
    local_centre = Vector((
        (max(c.x for c in corners) + min(c.x for c in corners)) * 0.5,
        (max(c.y for c in corners) + min(c.y for c in corners)) * 0.5,
        (max(c.z for c in corners) + min(c.z for c in corners)) * 0.5,
    ))
    centre = obj.matrix_world @ local_centre
    yaw = math.degrees(obj.matrix_world.to_euler("XYZ").z)
    return centre, size, yaw


def carve_gateway(centre, size, yaw, axis_y):
    """Split a box around a doorway. Returns a list of (centre, size).

    Without this the compound is sealed: the bridge runs up to a gatehouse that
    is one solid proxy, and the level's whole approach ends at a wall you can
    see through. The cut is only attempted on unyawed boxes that actually
    straddle the doorway, and it leaves the lintel above it in place so the
    gate still reads as an arch rather than a gap.

    `axis_y` is per-object rather than one constant for the map. There are two
    gates -- the outer one the bridge arrives at, and an inner one into the
    keep's enclosure 4 m further along Y -- and carving only the outer one
    leaves the keep in its own sealed region with the player locked out of it.
    """
    if abs(yaw) > 1e-3:
        return [(centre, size)]

    lo_y, hi_y = centre.y - size.y * 0.5, centre.y + size.y * 0.5
    door_lo, door_hi = axis_y - GATE_WIDTH * 0.5, axis_y + GATE_WIDTH * 0.5
    if lo_y > door_lo or hi_y < door_hi:
        return [(centre, size)]

    lo_z, hi_z = centre.z - size.z * 0.5, centre.z + size.z * 0.5
    door_top = lo_z + GATE_HEADROOM
    parts = []

    for a, b in ((lo_y, door_lo), (door_hi, hi_y)):
        if b - a > MIN_PART:
            parts.append((Vector((centre.x, (a + b) * 0.5, centre.z)),
                          Vector((size.x, b - a, size.z))))
    if hi_z - door_top > MIN_PART:
        parts.append((Vector((centre.x, axis_y, (door_top + hi_z) * 0.5)),
                      Vector((size.x, GATE_WIDTH, hi_z - door_top))))
    return parts or [(centre, size)]


MIN_PART = 0.15


def walkable_regions(heights, n):
    """Label cells by which region a character can actually walk around.

    Two neighbouring cells are connected when their proxy tops are within
    MAX_STEP, which is exactly the rule PhysicsManager applies when it decides
    whether a surface is a stair to step onto or a wall to be stopped by
    (PhysicsManager.cpp:176). Anything else in this file could be right and the
    level still be unplayable if the answer here is that the gate is in a
    different region from the spawn.
    """
    label = [[None] * n for _ in range(n)]
    sizes = []
    for j0 in range(n):
        for i0 in range(n):
            if heights[j0][i0] is None or label[j0][i0] is not None:
                continue
            region = len(sizes)
            stack = [(j0, i0)]
            label[j0][i0] = region
            count = 0
            while stack:
                j, i = stack.pop()
                count += 1
                for dj, di in ((0, 1), (1, 0), (0, -1), (-1, 0)):
                    nj, ni = j + dj, i + di
                    if not (0 <= nj < n and 0 <= ni < n):
                        continue
                    if heights[nj][ni] is None or label[nj][ni] is not None:
                        continue
                    if abs(heights[nj][ni] - heights[j][i]) > MAX_STEP + 1e-6:
                        continue
                    label[nj][ni] = region
                    stack.append((nj, ni))
            sizes.append(count)
    return label, sizes


def cell_of(point, origin, n, cell):
    i = int((point[0] - origin[0]) / cell)
    j = int((point[1] - origin[1]) / cell)
    if 0 <= i < n and 0 <= j < n:
        return j, i
    return None


def split_error(terrain, heights, n, origin, cell):
    """Ground error, separated into walkable ground and cliff faces.

    The overall figure is dominated by cells sitting on the ravine wall, where
    a single height cannot represent a 3 m cell spanning several metres of drop
    and no choice of parameters would make it. What matters is the error
    underfoot on ground the player can stand on, so that is reported apart from
    the rest instead of being averaged together with it.
    """
    flat, steep = [], []
    for j in range(n):
        for i in range(n):
            if heights[j][i] is None:
                continue
            gaps = [abs(heights[j + dj][i + di] - heights[j][i])
                    for dj, di in ((0, 1), (1, 0), (0, -1), (-1, 0))
                    if 0 <= j + dj < n and 0 <= i + di < n
                    and heights[j + dj][i + di] is not None]
            bucket = steep if (gaps and max(gaps) > MAX_STEP) else flat
            for fy in (0.25, 0.75):
                for fx in (0.25, 0.75):
                    z = terrain_height(terrain, origin[0] + (i + fx) * cell,
                                       origin[1] + (j + fy) * cell)
                    if z is not None:
                        bucket.append(abs(heights[j][i] - z))

    def stats(values):
        if not values:
            return None
        values = sorted(values)
        return {"n": len(values), "mean": sum(values) / len(values),
                "p95": values[int(0.95 * (len(values) - 1))],
                "max": values[-1]}

    return stats(flat), stats(steep)


def build_ground_proxies(collision, terrain, centre):
    heights, n, origin = sample_ground_grid(
        terrain, PLAY_RADIUS, GROUND_CELL, GROUND_SAMPLES, centre)
    converged = quantize_and_relax(heights, n, HEIGHT_QUANTUM, STEP_QUANTA,
                                   RELAX_ITERATIONS, CLIFF_QUANTA)
    error = measure_error(terrain, heights, n, origin, GROUND_CELL)
    rects = merge_rectangles(heights, n)

    for index, (i, j, w, h, z) in enumerate(rects):
        x0 = origin[0] + i * GROUND_CELL
        y0 = origin[1] + j * GROUND_CELL
        size_x, size_y = w * GROUND_CELL, h * GROUND_CELL
        add_box(collision, "BOX_Ground_%03d" % index,
                centre=(x0 + size_x * 0.5, y0 + size_y * 0.5,
                        z - GROUND_THICKNESS * 0.5),
                size=(size_x, size_y, GROUND_THICKNESS),
                yaw_deg=0.0, colour=COLOR_GROUND)

    worst, over, cliffs = report_steps(heights, n, MAX_STEP,
                                       CLIFF_QUANTA * HEIGHT_QUANTUM)
    cells = sum(1 for row in heights for v in row if v is not None)
    flat_err, steep_err = split_error(terrain, heights, n, origin, GROUND_CELL)

    label, sizes = walkable_regions(heights, n)
    landmarks = {
        "player spawn": PLAYER_SPAWN_XY,
        "bridge east": (-129.5, GATE_AXIS_Y),
        "gate": (-141.2, GATE_AXIS_Y),
        "compound": (-147.0, -16.0),
        "keep": (-157.5, -11.6),
    }
    where = {}
    for name, point in landmarks.items():
        rc = cell_of(point, origin, n, GROUND_CELL)
        region = None if rc is None else label[rc[0]][rc[1]]
        where[name] = (region, sizes[region] if region is not None else 0)

    return {"cells": cells, "boxes": len(rects), "converged": converged,
            "worst_step": worst, "steps_over_max": over, "cliffs": cliffs,
            "error": error, "flat_error": flat_err, "steep_error": steep_err,
            "regions": len(sizes), "landmarks": where}


def build_building_proxies(collision, depsgraph):
    made = carved = 0
    for obj in sorted(source_collection(BUILDINGS).objects,
                      key=lambda o: o.name):
        if obj.type != "MESH":
            continue
        centre, size, yaw = yaw_box_of(obj, depsgraph)
        bridge = obj.name.startswith("bridge")
        # Every piece named "entrance" is a way in, and each is carved on its
        # own centreline -- see carve_gateway.
        axis_y = centre.y if "entrance" in obj.name else None
        parts = ([(centre, size)] if axis_y is None
                 else carve_gateway(centre, size, yaw, axis_y))
        if len(parts) > 1:
            carved += 1
        safe = obj.name.replace(".", "_").replace(" ", "_")
        for k, (part_centre, part_size) in enumerate(parts):
            add_box(collision, "BOX_Bld_%s_%d" % (safe, k),
                    centre=tuple(part_centre), size=tuple(part_size),
                    yaw_deg=yaw,
                    colour=COLOR_BRIDGE if bridge else COLOR_BUILDING)
            made += 1
    return {"boxes": made, "carved": carved}


def ground_proxy_top(collision, x, y):
    """Top of the highest ground proxy covering (x, y), or None."""
    best = None
    for obj in collision.all_objects:
        if not obj.name.startswith("BOX_Ground_"):
            continue
        dx, dy = x - obj.location.x, y - obj.location.y
        yaw = -obj.rotation_euler.z
        lx = dx * math.cos(yaw) - dy * math.sin(yaw)
        ly = dx * math.sin(yaw) + dy * math.cos(yaw)
        if abs(lx) <= obj.scale.x * 0.5 and abs(ly) <= obj.scale.y * 0.5:
            top = obj.location.z + obj.scale.z * 0.5
            if best is None or top > best:
                best = top
    return best


def build_bridge_thresholds(collision, depsgraph):
    """Steps closing the gap between the ground proxies and the bridge deck.

    The deck and the ground meet at a bevel the eye reads straight over, but
    the collision surfaces they turn into do not meet at all: the ground grid
    quantises to 0.25 m and relaxes, and at the east end it settles 0.47 m
    above the deck. That is past MAX_STEP, so the bridge becomes a kerb the
    player cannot climb -- and since the bridge is the only way over the
    ravine, it alone makes the castle unreachable.

    Two details here are the whole fix, and both were wrong first time round:

    * The gap is measured against the ground *proxy*, not the terrain mesh.
      The player stands on the proxy, and here the two differ by more than the
      step budget being spent.
    * The steps go on the last stretch of the deck, not on the ground beyond
      it. The floor at any point is the highest box covering it, so a step
      tucked under the too-high ground proxy is simply never stood on; it has
      to sit where the deck is currently winning in order to raise it.
    """
    bridges = [o for o in source_collection(BUILDINGS).objects
               if o.name.startswith("bridge") and o.type == "MESH"]
    if not bridges:
        return {"boxes": 0, "gaps": []}

    corners = []
    for obj in bridges:
        evaluated = obj.evaluated_get(depsgraph)
        corners += [obj.matrix_world @ Vector(c) for c in evaluated.bound_box]
    lo = [min(c[k] for c in corners) for k in range(3)]
    hi = [max(c[k] for c in corners) for k in range(3)]
    deck, width = hi[2], hi[1] - lo[1]

    rise = STEP_QUANTA * HEIGHT_QUANTUM
    tread = 1.2                 # wider than the probe spacing and BODY_RADIUS
    mid_x, half_span = (lo[0] + hi[0]) * 0.5, (hi[0] - lo[0]) * 0.5
    made, gaps = 0, []
    for direction in (1.0, -1.0):
        # Where the landing first rises more than a step above the deck. Found
        # by walking out along the axis rather than assumed to be the bridge's
        # own end: the ground grid's plateau cell overlaps the last metre and a
        # half of the deck, so the point the player actually has to climb is
        # inboard of where the bridge stops.
        edge = None
        for s in range(int((half_span + 3.0) / 0.2)):
            x = mid_x + direction * 0.2 * s
            top = ground_proxy_top(collision, x, GATE_AXIS_Y)
            if top is not None and top > deck + MAX_STEP:
                edge = x
                break
        if edge is None:
            gaps.append(None)
            continue

        landing = ground_proxy_top(collision, edge + direction * 0.5,
                                   GATE_AXIS_Y)
        if landing is None:
            gaps.append(None)
            continue
        gap = landing - deck
        gaps.append(round(gap, 2))
        if abs(gap) <= MAX_STEP:
            continue
        count = int(math.ceil(abs(gap) / rise)) - 1
        for k in range(1, count + 1):
            z = deck + gap * k / float(count + 1)
            # Named as a bridge piece deliberately: check_walkability decides
            # what is a floor and what is a wall by this prefix, and a step
            # onto the deck classified as a wall seals the crossing instead of
            # opening it.
            add_box(collision,
                    "BOX_Bld_bridge_step_%s_%d"
                    % ("e" if direction > 0 else "w", k),
                    centre=(edge - direction * (tread * (count - k)
                                                + tread * 0.5),
                            (lo[1] + hi[1]) * 0.5, z - 1.0),
                    size=(tread, width, 2.0), yaw_deg=0.0,
                    colour=COLOR_BRIDGE)
            made += 1
    return {"boxes": made, "gaps": gaps}


def build_rock_proxies(collision, depsgraph):
    made = skipped = 0
    for obj in sorted(source_collection(ROCKS).objects, key=lambda o: o.name):
        if obj.type != "MESH":
            continue
        centre, size, yaw = yaw_box_of(obj, depsgraph)
        if max(size.x, size.y) < ROCK_MIN_FOOTPRINT:
            skipped += 1
            continue
        add_box(collision, "BOX_Rock_%s" % obj.name.replace(".", "_"),
                centre=tuple(centre), size=tuple(size), yaw_deg=yaw,
                colour=COLOR_ROCK)
        made += 1
    return {"boxes": made, "skipped": skipped}


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


WALK_PROBE = 1.0            # metres between walkability samples
BODY_HEIGHT = 1.8           # Player::BODY_HEIGHT


def check_walkability(collision, centre, landmarks):
    """Flood-fill the level to find out where the player can actually get to.

    The per-cell region check inside build_ground_proxies only sees the ground
    grid, and the ground grid does not span the ravine -- the bridge does, and
    the bridge is a building proxy. So the question that actually matters,
    "can the player get from the spawn to the keep", cannot be answered there.

    Proxies are split into two roles, which is the part that has to be right.
    Ground and bridge boxes are *floors*: the support height at a probe is the
    highest of them covering it, matching the surface PhysicsManager's scan
    settles on (PhysicsManager.cpp:347-376). Walls, buildings, rocks, trees and
    the boundary ring are *blockers*: they make a probe unwalkable if they
    occupy the body above the floor there. Treating those as floors instead --
    standing on top of a tree trunk, on top of a wall -- is technically what
    the engine would do to a character teleported onto one, but as a
    reachability model it is useless: every trunk becomes its own one-probe
    region 10 m up and the map shatters into hundreds of islands.
    """
    floors, blockers = [], []
    for obj in collision.all_objects:
        half = (obj.scale.x * 0.5, obj.scale.y * 0.5)
        top = obj.location.z + obj.scale.z * 0.5
        bottom = obj.location.z - obj.scale.z * 0.5
        yaw = obj.rotation_euler.z
        entry = (obj.location.x, obj.location.y, half[0], half[1],
                 math.cos(-yaw), math.sin(-yaw), top, bottom,
                 math.hypot(*half))
        is_floor = (obj.name.startswith("BOX_Ground_")
                    or obj.name.startswith("BOX_Bld_bridge"))
        (floors if is_floor else blockers).append(entry)

    bucket_size = 8.0

    def index(entries):
        buckets = defaultdict(list)
        for entry in entries:
            cx, cy, reach = entry[0], entry[1], entry[8]
            for bi in range(int((cx - reach) // bucket_size),
                            int((cx + reach) // bucket_size) + 1):
                for bj in range(int((cy - reach) // bucket_size),
                                int((cy + reach) // bucket_size) + 1):
                    buckets[(bi, bj)].append(entry)
        return buckets

    floor_index, blocker_index = index(floors), index(blockers)

    def covers(entry, x, y):
        cx, cy, hx, hy, cos_y, sin_y = entry[:6]
        dx, dy = x - cx, y - cy
        return (abs(dx * cos_y - dy * sin_y) <= hx
                and abs(dx * sin_y + dy * cos_y) <= hy)

    def probe(x, y):
        key = (int(x // bucket_size), int(y // bucket_size))
        support = None
        for entry in floor_index.get(key, ()):
            if covers(entry, x, y) and (support is None or entry[6] > support):
                support = entry[6]
        if support is None:
            return None
        # Anything intruding into the body standing on that floor blocks it.
        # The 0.05 m lift keeps a wall whose base is flush with the ground from
        # blocking the ground it is standing on.
        low, high = support + 0.05, support + BODY_HEIGHT
        for entry in blocker_index.get(key, ()):
            if entry[7] < high and entry[6] > low and covers(entry, x, y):
                return None
        return support

    n = int(2.0 * PLAY_RADIUS / WALK_PROBE)
    origin = (centre[0] - PLAY_RADIUS, centre[1] - PLAY_RADIUS)
    heights = [[probe(origin[0] + (i + 0.5) * WALK_PROBE,
                      origin[1] + (j + 0.5) * WALK_PROBE)
                for i in range(n)] for j in range(n)]

    label, sizes = walkable_regions(heights, n)
    out = {}
    for name, point in landmarks.items():
        rc = cell_of(point, origin, n, WALK_PROBE)
        region = None if rc is None else label[rc[0]][rc[1]]
        out[name] = (region, sizes[region] if region is not None else 0)
    walkable = sum(1 for row in heights for v in row if v is not None)
    biggest = sorted(sizes, reverse=True)[:5]
    return out, walkable * WALK_PROBE * WALK_PROBE, len(sizes), biggest


def shift_collection(collection, offset):
    """Move generated proxies into the same recentred frame as VISUAL.

    Everything above is authored against the terrain where it actually sits,
    which is 158 m from the origin, so the numbers in this file can be read
    against what the .blend shows. VISUAL is built in that frame too and then
    shifted; the proxies have to ride the identical shift or they end up
    wrapping geometry that is no longer there.
    """
    for obj in collection.all_objects:
        obj.location = obj.location + Vector(offset)


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
    markers = get_collection(MARKERS)
    for generated in (visual, collision, markers):
        clear_collection(generated)

    report = {"props_before": 0, "props_after": 0, "scatter_after": 0}

    entries, backdrop = build_terrain(report, centre)
    entries.extend(build_props(report))
    scatter, island_trees = build_scatter(report, centre)
    entries.extend(scatter)

    raw_tris = sum(tri_count(mesh) for mesh, _ in entries + backdrop)
    raw_pieces = len(entries) + len(backdrop)
    meshes = assemble(entries, backdrop, visual, RECENTRE)

    # Collision is authored against the terrain where it actually sits and
    # shifted afterwards, so it has to be built before the sources are hidden
    # -- a hidden collection is out of the depsgraph, and terrain_height would
    # raycast an object with no evaluated mesh.
    depsgraph = bpy.context.evaluated_depsgraph_get()
    proxies = {
        "ground": build_ground_proxies(collision, ground, centre),
        "buildings": build_building_proxies(collision, depsgraph),
        "thresholds": build_bridge_thresholds(collision, depsgraph),
        "rocks": build_rock_proxies(collision, depsgraph),
        "trees": build_tree_proxies(collision, island_trees),
        "boundary": build_boundary(collision, ground, centre),
    }
    spawns = build_markers(markers, ground)

    reach, walk_area, walk_regions, big_regions = check_walkability(
        collision, centre, {
        "player spawn": PLAYER_SPAWN_XY,
        "bridge middle": (-134.0, GATE_AXIS_Y),
        "gate": (-141.2, GATE_AXIS_Y),
        "courtyard": (-147.0, -16.0),
        "keep approach": (-152.0, -16.0),
    })

    shift_collection(collision, RECENTRE)
    shift_collection(markers, RECENTRE)

    set_sources_hidden(True)

    # COLLISION and MARKERS must stay visible, and not as a matter of taste.
    # export_level.py reads every proxy's matrix_world, and Blender only
    # computes matrix_world for objects the depsgraph evaluates -- a hidden
    # collection is not evaluated, so its objects keep an identity matrix and
    # the export writes the entire level's collision and both sets of spawns at
    # the origin. It does that silently: verify_level.py's bounds check is
    # dominated by the visual mesh, and its per-proxy check needs VIS_* meshes
    # this kitbashed map does not have, so nothing downstream notices.
    for generated in (collision, markers):
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
        if span > CHUNK_SIZE * 2.0:
            uncullable += tri_count(obj.data)

    print("\n[make_castle_level] island centre %.1f, %.1f -> recentred by %s"
          % (centre[0], centre[1], tuple(round(c, 1) for c in RECENTRE)))
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
    print("[make_castle_level] cull      widest cullable chunk %.1f m (%s); "
          "backdrop %d tris always drawn"
          % (widest, widest_name, backdrop_tris))
    if total_tris != raw_tris:
        print("[make_castle_level] WARNING merge changed the triangle count "
              "(%d -> %d)" % (raw_tris, total_tris))
    g = proxies["ground"]
    total_proxies = sum(p["boxes"] for p in proxies.values())
    print("[make_castle_level] collision %d proxies -- ground %d (from %d "
          "cells), buildings %d (%d carved for the gate), rocks %d (%d too "
          "small), trees %d, boundary %d, bridge thresholds %d for gaps %s"
          % (total_proxies, g["boxes"], g["cells"],
             proxies["buildings"]["boxes"], proxies["buildings"]["carved"],
             proxies["rocks"]["boxes"], proxies["rocks"]["skipped"],
             proxies["trees"]["boxes"], proxies["boundary"]["boxes"],
             proxies["thresholds"]["boxes"], proxies["thresholds"]["gaps"]))
    print("[make_castle_level]   worst walkable step %.2f m (%d over MAX_STEP "
          "%.2f), %d deliberate cliffs left as walls%s"
          % (g["worst_step"], g["steps_over_max"], MAX_STEP, g["cliffs"],
             "" if g["converged"] else "  [relaxation did NOT converge]"))
    for label, stats in (("walkable ground", g["flat_error"]),
                         ("cliff faces   ", g["steep_error"])):
        if stats:
            print("[make_castle_level]   error on %s: mean %.2f m, p95 %.2f m,"
                  " max %.2f m (%d samples)"
                  % (label, stats["mean"], stats["p95"], stats["max"],
                     stats["n"]))
    spawn_region = reach["player spawn"][0]
    print("[make_castle_level] walkable  %.0f m2 over all proxies, %d regions "
          "(largest %s m2)" % (walk_area, walk_regions,
                               [int(b * WALK_PROBE ** 2) for b in big_regions]))
    for name, (region, size) in reach.items():
        mark = "  <-- NOT REACHABLE FROM SPAWN" if (
            region is None or region != spawn_region) else ""
        print("      %-14s %s%s"
              % (name, "no floor" if region is None
                 else "region %d, %.0f m2" % (region, size * WALK_PROBE ** 2),
                 mark))
    print("[make_castle_level] spawns    %s" % (spawns,))

    if uncullable:
        # Worth failing loudly over. Triangles in a chunk this wide are drawn
        # every frame into the camera pass and every shadow cascade no
        # matter where the player is standing, which is the one outcome this
        # whole script exists to avoid.
        print("[make_castle_level] WARNING %d tris sit in chunks wider than "
              "%.0f m and will never be culled" % (uncullable, CHUNK_SIZE * 2))

    if not args.no_save:
        out = args.out or bpy.data.filepath
        bpy.ops.wm.save_as_mainfile(filepath=out)
        print("[make_castle_level] saved %s (%.1f MB)"
              % (out, os.path.getsize(out) / 1e6))


if __name__ == "__main__":
    main()
