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

A generated blade is three triangles with no transparency, and its colour is
*read off the terrain material* (see `terrain_colour`), so it matches by
construction and goes on matching when the ground is re-tinted -- which is what
tools/tint_ground.py is for.

Both objections still stand against a stylised cut-out, not just a photographed
one: a painted blade sheet is as transparent per quad as a photographed one, and
its green is as fixed. What the asset would have bought -- a shape that does not
read as a spike -- is bought here instead by giving the blade a curve, a short
taper and a tuft to grow in, which costs geometry rather than fill rate and
keeps the colour coupling intact.

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

That covers every phase but one. In phase2 the castle island's ground carries
the same `grass` material as the approach on the far side of the bridge, and is
just as flat and just as dry, so no test this tool can make separates them --
the island is meadow by every one of them. Where the material cannot answer the
question, MEADOW_REGIONS does, in plan-space coordinates measured off the
level as built. It is deliberately the exception and not the mechanism: a phase
with no entry grows grass wherever it is green.

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
        [--density 9] [--max-blades 320000] [--width 0.115] [--height 1.00]
        [--level <phase>] [--region minx,miny,maxx,maxy]

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

# A three-segment blade that arcs: pairs of verts at the foot and at two rings
# up the blade, one at the tip. Seven vertices, five triangles.
#
# This replaced a single isoceles triangle, and the note that rejected the curve
# the first time round measured the wrong thing. It compared *centreline
# deviation in pixels* -- about two on the nearest blades, under one past 8 m --
# and concluded the curve was invisible. It is not a pixel-accuracy question.
# Straight edges converging on a point read as a triangle at any distance,
# because the eye reads the silhouette's shape language and not its error
# against some reference.
#
# Three segments and not two, which was tried in between and rendered. Two
# segments do not curve -- they kink. A blade is a straight piece, one visible
# corner, then another straight piece, and at the camera's 4.2 m the corner is
# what the eye finds. The third segment is what turns that corner into an arc,
# and it is the whole reason this looks like grass rather than like bent wire.
VERTS_PER_BLADE = 7
TRIS_PER_BLADE = 5

# Corner indices into the seven, in the order blade_geometry emits them: rings
# of two at 0-1, 2-3 and 4-5 going up, then the tip alone at 6. Winding is
# consistent but not load-bearing -- GameRenderer draws the meadow with
# backface culling off, and grass.fs lights from a constant up vector rather
# than from geometry, so neither face has a front to be on.
BLADE_TRIS = ((0, 1, 3), (0, 3, 2),
              (2, 3, 5), (2, 5, 4),
              (4, 5, 6))

# Where each ring sits along the blade, as a fraction of its length. The first
# is the foot and the last is the tip, so this is also what sets
# VERTS_PER_BLADE: two verts for every ring but the tip, which is one.
RINGS = (0.0, 0.38, 0.72, 1.0)

# Half-width at each of those rings, as a fraction of BLADE_WIDTH.
#
# The blade holds nearly all its width to 72% of its length and then narrows
# over the last quarter. That is what keeps it a ribbon: taper it evenly from
# the ground up -- which is what a single triangle does by definition -- and it
# is a wedge again however hard it is curved.
#
# The tip is a single vertex, so it is a point rather than the blunt end an
# eighth vertex would buy. At 4.2 m a fine point on the end of an arc reads as
# a grass tip; what read as a spike was the straightness, not the sharpness.
WIDTH_PROFILE = (1.0, 0.95, 0.66, 0.0)

# Base width in metres. Reach for this before BLADE_DENSITY when the meadow
# looks thin: coverage across a view is proportional to density * depth *
# width, so widening a blade buys the same fullness as adding blades and costs
# no triangles and no bytes at all. Raised from 7 cm alongside the curve, which
# is how the triangle budget stays near where it was -- see BLADE_DENSITY.
BLADE_WIDTH = 0.115
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

# How far the blade's tangent has turned off the ground normal, at its root and
# at its tip. The blade leaves the ground nearly upright and keeps bending, so
# the difference between these two is the curve; LEAN used to be a single angle
# held all the way up, which is a straight stalk.
#
# No 1/cos correction any more. blade_curve builds the arc at unit length,
# measures how much height that actually won, and scales -- so a standing
# height still means what it says however far the blade arcs over, and it stays
# right for a curve where no single cosine would.
ROOT_LEAN = (4.0, 16.0)

# Past 90 degrees on purpose for the upper end of the range: those blades arch
# over and point their tips back down at the ground, which is the shape that
# makes a tuft read as a fountain rather than as a brush. blade_curve measures a
# blade's height at its highest point rather than at its tip precisely so that
# these still stand as tall as they are asked to.
TIP_LEAN = (52.0, 112.0)

# Exponent on the root-to-tip interpolation of that angle. Above 1.0 the blade
# holds its root angle longer and then whips over near the tip, which is the
# stylised profile -- a stiff stalk with a soft end. At 1.0 the curvature is
# uniform and it reads as a bent wire.
CURVE_BIAS = 1.7

# Integration steps for that arc. Past ~16 the ring positions move by well under
# a millimetre, so this is not a quality dial; it is only here to be a named
# number rather than a literal in the loop.
CURVE_STEPS = 24

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

# Warmth added toward the tip, as a fraction pushed into red and out of blue.
# Stylised grass is not one green darkened -- the base sits cool in its own
# shadow and the tip catches sun, so the gradient carries hue as well as value.
# Small: this is multiplying an already-green baseColorFactor, so 0.10 here is
# a visible shift rather than a subtle one.
TIP_WARMTH = 0.10

# Per-blade variation, so a meadow is not one flat field of the same green.
VALUE_JITTER = 0.08
HUE_JITTER = 0.05

# --- Clumping --------------------------------------------------------------

# Blades per tuft, and the radius they are scattered within.
#
# The scatter used to be uniform over the whole meadow, and uniform is what a
# field of spikes looks like: every blade the same distance from its neighbours,
# no structure at any scale between one blade and the whole level. Real grass --
# and every stylised grass that reads as grass -- grows in tufts, and the tuft
# is the thing the eye actually resolves at 4.2 m.
#
# This costs nothing. The same blade count is spent; only where it lands
# changes.
TUFT_BLADES = (5, 11)
TUFT_RADIUS = 0.20

# Mean members of a tuft, which is what turns a cell's blade budget into a
# number of tufts. Derived and not typed, so it cannot drift out of step with
# TUFT_BLADES -- and it has to be right, because the budget is spent in whole
# tufts and any error here shows up as a density that is not the one asked for.
MEAN_TUFT_BLADES = (TUFT_BLADES[0] + TUFT_BLADES[1]) / 2.0

# Blades lean *away* from the tuft's centre, so the clump opens like a fountain
# rather than combing one way. This is how far a blade's azimuth is allowed to
# wander off that outward direction.
TUFT_SPLAY = 42.0

# How much of the tip lean a blade at the centre of a tuft keeps. The middle of
# a clump stands up and the outside flops, which is what gives a tuft a profile
# instead of a uniform brush.
TUFT_CENTRE_LEAN = 0.45

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

# --- Where each phase grows grass ------------------------------------------

# Plan-space limits on the scatter, keyed by the .blend's own stem. A phase
# absent from here grows grass on every `grass` face it has, which is what
# phase1 and phase3 want: there the material is already the whole answer.
#
# phase2 is the exception, and the reason is worth stating because it is not
# visible from the material list. The castle island's ground carries the *same*
# `grass` material as the approach does -- it is one authored `ground` object,
# 73,106 faces of it -- and it is flat, dry and unshaded. Every test this tool
# has says meadow. So the only thing that can separate the island from the
# approach is where it is, and that is what this table is.
#
# Coordinates are the shipped frame, the one VISUAL and COLLISION are in, NOT
# the authored frame the `landscape` collection sits in. The two differ by a
# shift and a x3 scale that nobody has written down (see the module docstring),
# so these were measured off the level as built -- from the bridge's own
# collision railings -- and not read off the .blend's landscape objects.
#
# The margin past the bridge is not decoration: a blade rooted on the line
# still arcs up to 1.2 m westward, and the westernmost vertex this actually
# exports is x 135.2, against a bridge that ends at 134.5.
MEADOW_REGIONS = {
    # East of the bridge only: the approach the player spawns on, and nothing
    # across the water. BOX_Railing_bridge_0/1 put the bridge at x 94.3 to
    # 134.5, so this is its far end plus a couple of metres so that no blade
    # sprouts from the abutment itself.
    "phase2_approach": {"min_x": 136.5},
}

# --- Budget ----------------------------------------------------------------

# Blades per square metre, and the number this tool exists to let you turn.
#
# Uniform now, so it is the whole level rather than a peak most of the map sat
# below: 24 costs phase1_forest roughly 300,000 triangles and a 20 MB
# detail.glb, where the same figure under the old trail falloff bought about
# half that. Reach for --width before this when the meadow looks thin -- a wider
# blade covers the same ground for no triangles at all.
#
# Halved from 24 when the blade went from one triangle to three, and the meadow
# did not get thinner: BLADE_WIDTH went up by a third at the same time, tufts
# concentrate the blades that remain, and a curved blade covers more of its own
# bounding box than a wedge does. Triangle count lands around 1.5x the old
# figure and detail.glb comes out about where it was.
BLADE_DENSITY = 9.0

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
#
# A guard rail and not a design target: BLADE_DENSITY is the dial, and this only
# exists to stop a level nobody measured from shipping a gigabyte. Set high
# enough that no phase currently binds against it -- phase3's battlefield is
# 34,000 m^2, more than twice phase1's meadow, and holding it to the old 150,000
# gave it 4.2 blades/m^2 against the 8.5 the other two get. Half the density
# reads as half a meadow, so the ceiling moved rather than the battlefield
# staying thin.
#
# What it costs is file size and VRAM, not frame time. GameRenderer culls the
# detail meshes by frustum and by kGrassDrawDistance, so what is actually drawn
# is set by the density inside an 85 m disc and is the same on a small level as
# on a large one. A bigger level at the same density costs more bytes on disk
# and nothing per frame.
MAX_BLADES = 320000

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

    def __init__(self, visual, region=None):
        self.targets, self.face_material = scatter_targets(visual)
        self.path = material_tree(visual, PATH_MATERIAL)
        self.water = water_level(visual)
        (self.min_x, self.min_y), (self.max_x, self.max_y), top = \
            ground_bounds(visual)

        # The region is applied here, to the bounds the classification grid is
        # laid over, rather than as a per-sample test inside classify(). Same
        # answer, and it is the difference between scanning 400 m of phase2 to
        # throw away 85% of it and scanning only the strip that can survive.
        self.region = dict(region or {})
        self.min_x = max(self.min_x, self.region.get("min_x", -1e30))
        self.min_y = max(self.min_y, self.region.get("min_y", -1e30))
        self.max_x = min(self.max_x, self.region.get("max_x", 1e30))
        self.max_y = min(self.max_y, self.region.get("max_y", 1e30))
        if self.min_x >= self.max_x or self.min_y >= self.max_y:
            raise BuildError(
                "the meadow region %r leaves no ground: x %.1f..%.1f, y "
                "%.1f..%.1f. These are shipped-frame coordinates, not the "
                "authored frame the `landscape` collection is in."
                % (self.region, self.min_x, self.max_x, self.min_y, self.max_y))

        # Well clear of the tallest canopy: a ray that starts inside a tree
        # misses it and plants grass under the branches. Measured over the
        # whole level and not just the region -- higher is never wrong, and a
        # region that clipped a tall canopy out of this would bury grass.
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


def shade_colour(fraction, tint):
    """One COLOR_0 value for a point `fraction` of the way up a blade.

    glTF multiplies baseColorFactor by COLOR_0, and baseColorFactor already
    carries the terrain green -- so putting the green in here as well would
    square it. What goes in is how *dark* this point of the blade is, nudged
    per blade so the meadow is not one flat sheet of a single value.

    Alpha is not a colour at all: it is the height fraction itself, 0 at the
    foot and 1 at the tip, and grass.vs uses it as the mask that pins a blade's
    root to the ground while its tip moves in the wind. It rides in this
    attribute because COLOR_0 is already being exported and a second one would
    be another accessor per mesh for four bytes of the same information. Safe
    to put there because grass.fs writes alpha 1.0 unconditionally and the
    blade material is OPAQUE, so nothing downstream reads it as coverage.
    """
    level = SHADE_BASE + (SHADE_TIP - SHADE_BASE) * fraction
    warm = TIP_WARMTH * fraction
    shift = (1.0 + warm, 1.0, 1.0 - warm)
    return tuple(max(0.0, min(1.0, level * tint[i] * shift[i]))
                 for i in range(3)) + (fraction,)


def blade_curve(normal, direction, root_lean, tip_lean):
    """Ring points along a unit-length blade, plus the height it stands.

    Integrated rather than interpolated between two endpoints. A blade here is
    defined by how its *tangent* turns along its length -- from `root_lean` off
    the ground normal at the foot to `tip_lean` at the tip -- so the shape has
    to be walked; lerping two endpoints would give the straight stalk this
    replaced.

    Built at unit length so the caller can scale it. The second return value is
    the greatest height the blade reaches along `normal` per unit of length,
    which is what makes that scale exact: a standing height still means what it
    says however far the blade arcs over, and it stays right for a curve where
    no single cosine would.

    The *greatest* height and not the tip's, which matters once TIP_LEAN is
    allowed past 90 degrees. Such a blade arches over and its tip comes back
    down, so scaling by the tip would divide by a number heading for zero and
    grow the blade without bound to keep a tip that is falling at a height it
    has already left.
    """
    path = [Vector((0.0, 0.0, 0.0))]
    step = 1.0 / CURVE_STEPS
    for i in range(CURVE_STEPS):
        # Midpoint of the step, not its start -- with CURVE_BIAS bending the
        # angle this is a meaningfully better integral for the same work.
        along = (i + 0.5) / CURVE_STEPS
        angle = root_lean + (tip_lean - root_lean) * (along ** CURVE_BIAS)
        path.append(path[-1] + (normal * math.cos(angle)
                                + direction * math.sin(angle)) * step)

    rings = []
    for fraction in RINGS:
        where = fraction * CURVE_STEPS
        low = min(int(where), CURVE_STEPS - 1)
        rings.append(path[low].lerp(path[low + 1], where - low))
    return rings, max(point.dot(normal) for point in path)


def blade_geometry(foot, normal, direction, side, height, half_width,
                   root_lean, tip_lean, tint):
    """Five world verts and their five COLOR_0 values, base ring to tip.

    Order is (base left, base right, mid left, mid right, tip), which is what
    BLADE_TRIS indexes.

    `side` is fixed for the whole blade rather than recomputed per ring. It is
    perpendicular to the lean, so a blade that has tipped over shows its face
    to a viewer standing beside it -- twisting it to follow the curve would
    turn the tip edge-on and lose exactly the part of the silhouette the curve
    was added to show.
    """
    rings, rise = blade_curve(normal, direction, root_lean, tip_lean)
    # The first step of the integral leaves the ground at ROOT_LEAN, at most 16
    # degrees off the normal, so `rise` is never below cos(16 deg) of that first
    # step and this cannot approach a divide by zero.
    scale = height / rise

    verts, colours = [], []
    last = len(RINGS) - 1
    for index, fraction in enumerate(RINGS):
        centre = foot + rings[index] * scale
        colour = shade_colour(fraction, tint)
        if index == last:
            verts.append(centre[:])
            colours.append(colour)
        else:
            half = side * (half_width * WIDTH_PROFILE[index])
            verts.extend(((centre - half)[:], (centre + half)[:]))
            colours.extend((colour, colour))
    return verts, colours


def grow_blades(ground, cells, spacing, rng, options, report):
    """A blade list: (x, y, five world verts, five vertex colours).

    Each classified cell is filled independently, in tufts rather than one
    blade at a time -- a tuft picks a centre in the cell and its members land
    within TUFT_RADIUS of it, leaning away from that centre. The blade budget
    is spent the same either way; only where the blades land changes.

    Every blade still gets its own downward ray, including the ones sharing a
    tuft. That ray is the reason blades sit flush across a terrain crease: the
    mesh runs about 0.6 triangles per square metre, so even a 20 cm tuft can
    straddle an edge, and reusing the tuft centre's hit would float or bury
    the outer members.
    """
    cell_area = spacing * spacing
    projected = options["density"] * cell_area * len(cells)
    scale = 1.0 if projected <= options["max_blades"] \
        else options["max_blades"] / projected
    report["projected"] = projected
    report["scale"] = scale

    # Uniform, so the per-cell figure is the same everywhere and only its
    # fractional part is resolved per cell. Stochastic rounding rather than a
    # floor, or a density of 24.6 would scatter 24 and quietly lose 2.5%.
    #
    # Counted in *tufts*, not blades, and that is not a presentational choice.
    # The budget is spent in whole tufts -- a partial tuft is a runt of two or
    # three blades and looks like one -- so laying tufts down until a blade
    # budget is met overshoots it by most of a tuft every time. With ~8 blades
    # to a tuft and a cell wanting 10, that is two tufts and 16 blades: 60%
    # over, every cell, and --max-blades stops being a ceiling at all. Dividing
    # first makes the blade count come out right in expectation and leaves
    # every tuft full.
    wanted = options["density"] * cell_area * scale / MEAN_TUFT_BLADES
    whole, fraction = int(wanted), wanted - int(wanted)

    counts = {"tried": 0, "missed": 0, "not_ground": 0, "steep": 0,
              "drowned": 0, "tufts": 0}
    blades = []
    step = max(1, len(cells) // 10)

    for index, (cell_x, cell_y) in enumerate(cells):
        if index and index % step == 0:
            print("[scatter_grass]   ...%3d%%  %d blades"
                  % (100 * index // len(cells), len(blades)))

        for _ in range(whole + (1 if rng.random() < fraction else 0)):
            counts["tufts"] += 1
            tuft_x = cell_x + rng.random() * spacing
            tuft_y = cell_y + rng.random() * spacing

            # Shared across the tuft, which is the point of having tufts: one
            # clump is one plant, so its blades agree on height and colour and
            # the variation the eye reads is between clumps rather than
            # between neighbouring blades.
            tuft_height = options["height"] * rng.uniform(*HEIGHT_JITTER)
            value = 1.0 + rng.uniform(-VALUE_JITTER, VALUE_JITTER)
            hue = rng.uniform(-HUE_JITTER, HUE_JITTER)
            tint = (value * (1.0 - 0.8 * hue), value * (1.0 + hue), value)

            for _ in range(rng.randint(*TUFT_BLADES)):
                counts["tried"] += 1

                # Square-rooted so the members spread evenly over the disc
                # instead of bunching at its centre.
                azimuth = rng.uniform(0.0, math.tau)
                reach = math.sqrt(rng.random())
                offset = TUFT_RADIUS * reach
                x = tuft_x + math.cos(azimuth) * offset
                y = tuft_y + math.sin(azimuth) * offset

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

                # Away from the tuft's centre, jittered. A clump that opens
                # outward reads as one plant; give every member an independent
                # azimuth and the tuft is just a dense patch of the old
                # uniform scatter.
                splay = math.radians(rng.uniform(-TUFT_SPLAY, TUFT_SPLAY))
                direction, side = blade_frame(normal, azimuth + splay)

                # The centre of a clump stands up and the outside flops, which
                # is what gives a tuft a profile rather than a flat brush.
                lean_scale = TUFT_CENTRE_LEAN \
                    + (1.0 - TUFT_CENTRE_LEAN) * reach
                root_lean = math.radians(rng.uniform(*ROOT_LEAN)) * lean_scale
                tip_lean = math.radians(rng.uniform(*TIP_LEAN)) * lean_scale

                half_width = (options["width"]
                              * rng.uniform(*WIDTH_JITTER) * 0.5)

                verts, colours = blade_geometry(
                    hit - normal * SINK, normal, direction, side,
                    tuft_height, half_width, root_lean, tip_lean, tint)
                blades.append((hit.x, hit.y, verts, colours))

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
                tris.extend(tuple(base + corner for corner in tri)
                            for tri in BLADE_TRIS)

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
    parser.add_argument("--level", default=None,
                        help="which MEADOW_REGIONS entry to use (default: the "
                             ".blend's own stem)")
    parser.add_argument("--region", default=None,
                        help="override MEADOW_REGIONS for this run, as "
                             "minx,miny,maxx,maxy in the shipped frame. Any "
                             "field may be blank for unbounded, e.g. "
                             "'136.5,,,' for everything east of x=136.5.")
    return parser.parse_args(argv)


def resolve_region(args):
    """(region dict, where it came from) for this run."""
    if args.region is not None:
        fields = args.region.split(",")
        if len(fields) != 4:
            raise BuildError("--region wants four comma-separated fields "
                             "(minx,miny,maxx,maxy), got %r" % args.region)
        keys = ("min_x", "min_y", "max_x", "max_y")
        region = {k: float(v) for k, v in zip(keys, fields) if v.strip()}
        return region, "--region"

    level = args.level
    if level is None:
        stem = os.path.basename(bpy.data.filepath)
        level = os.path.splitext(stem)[0]
    if level in MEADOW_REGIONS:
        return MEADOW_REGIONS[level], "MEADOW_REGIONS[%r]" % level
    return None, "whole level (no MEADOW_REGIONS entry for %r)" % level


def main():
    args = parse_args(sys.argv)

    visual = collection(VISUAL)
    detail = detail_collection()
    report = {"removed": clear_previous((visual, detail))}

    colour = terrain_colour()
    material = blade_material(colour)

    region, region_source = resolve_region(args)

    rng = random.Random(SEED)
    spacing = 1.0 / math.sqrt(SITE_DENSITY)
    ground = Ground(visual, region)

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
    print("[scatter_grass] blade      %.2f m tall (x%.2f..%.2f), %.1f cm wide, "
          "arcing %.0f-%.0f to %.0f-%.0f deg over %d tris"
          % (args.height, HEIGHT_JITTER[0], HEIGHT_JITTER[1],
             args.width * 100.0, ROOT_LEAN[0], ROOT_LEAN[1],
             TIP_LEAN[0], TIP_LEAN[1], TRIS_PER_BLADE))
    print("[scatter_grass] tufts      %d clumps of %d-%d, %.0f cm across, "
          "splaying +/-%.0f deg"
          % (grown["tufts"], TUFT_BLADES[0], TUFT_BLADES[1],
             TUFT_RADIUS * 200.0, TUFT_SPLAY))
    print("[scatter_grass] falloff    none -- uniform over every green cell, "
          "%.1f m clear of the trail" % PATH_CLEAR)
    print("[scatter_grass] region     x %.1f..%.1f, y %.1f..%.1f  (%s)"
          % (ground.min_x, ground.max_x, ground.min_y, ground.max_y,
             region_source))
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
