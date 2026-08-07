"""Generate a deliberately heavy level, to measure how rendering cost scales.

The greybox is 132 triangles. That is fine for proving the export pipeline, and
useless for answering "does the shadow pass explode as the map grows" -- at that
size the depth pass is 99.8% character geometry and the level contributes
nothing measurable either way.

This builds a level in the size and density range a kitbashed map actually lands
in: a subdivided ground plane plus a few hundred scattered blocks, spread over
150m. Roughly 200k triangles by default, which is what a Sketchfab/Polyhaven
kitbash of that footprint costs before decimation.

It is a measurement fixture, not a playable map. The layout is a grid of
obstacles with no design intent -- what matters is triangle count, mesh count and
spatial spread, because those are what culling and cascade caching act on.

Usage:
    blender --background --python tools/make_stress_level.py -- \
        [--out source/levels/stress.blend] [--extent 150] [--grid 300] [--blocks 200]

    blender --background source/levels/stress.blend \
        --python tools/export_level.py -- --out-dir assets/levels/stress
"""

import argparse
import math
import os
import random
import sys

import bpy

# Fixed so successive runs produce the same fixture and two measurements are
# comparable. A benchmark that changes shape between runs measures nothing.
SEED = 20260807

STONE = (150, 148, 140)
SLATE = (96, 100, 108)
MOSS = (104, 122, 84)


def to_blender(p):
    """Game (Y-up) -> Blender (Z-up). Inverse of export_level.py's to_game."""
    gx, gy, gz = p
    return (gx, -gz, gy)


def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0


def make_collection(name):
    collection = bpy.data.collections.new(name)
    bpy.context.scene.collection.children.link(collection)
    return collection


def make_material(name, rgb):
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    bsdf = material.node_tree.nodes.get("Principled BSDF")
    if bsdf is not None:
        # sRGB straight through -- see make_greybox_level.py for why this is
        # deliberately not linearised.
        bsdf.inputs["Base Color"].default_value = (*[c / 255.0 for c in rgb], 1.0)
    return material


def add_mesh(collection, name, verts, faces, rgb, material=None):
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    obj.color = (*[c / 255.0 for c in rgb], 1.0)
    if material is not None:
        mesh.materials.append(material)
    collection.objects.link(obj)
    return obj


BOX_FACES = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
             (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]


def box_verts(half):
    hx, hy, hz = half
    return [(-hx, -hy, -hz), (hx, -hy, -hz), (hx, hy, -hz), (-hx, hy, -hz),
            (-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz)]


def subdivided_plane(extent, divisions):
    """A flat grid in Blender space, centred at the origin, top at z=0.

    Subdivided rather than a single quad because a kitbashed ground is never two
    triangles -- it is terrain, paving, or a mesh carrying baked detail, and the
    vertex load it puts on the depth pass is a real part of what is being
    measured.
    """
    verts, faces = [], []
    step = (extent * 2.0) / divisions
    for row in range(divisions + 1):
        for col in range(divisions + 1):
            verts.append((-extent + col * step, -extent + row * step, 0.0))
    stride = divisions + 1
    for row in range(divisions):
        for col in range(divisions):
            a = row * stride + col
            faces.append((a, a + 1, a + stride + 1, a + stride))
    return verts, faces


def build(extent, grid, blocks):
    random.seed(SEED)
    reset_scene()
    visual = make_collection("VISUAL")
    collision = make_collection("COLLISION")
    markers = make_collection("MARKERS")
    collision.hide_render = True
    markers.hide_render = True

    ground_mat = make_material("MAT_Ground", MOSS)
    stone_mat = make_material("MAT_Stone", STONE)
    slate_mat = make_material("MAT_Slate", SLATE)

    # --- Ground: dense visual mesh, single flat collision box underneath ------
    verts, faces = subdivided_plane(extent, grid)
    add_mesh(visual, "VIS_Ground", verts, faces, MOSS, ground_mat)

    # The collision proxy is deliberately NOT subdivided. This is the whole
    # point of the two-file split: the mesh carries the detail, the proxy
    # carries the physics, and the proxy stays cheap however heavy the art gets.
    # Half-extents here are in BLENDER order (x, y, z), so the 1.0 of thickness
    # goes in z -- Blender's up. Writing it in game order (x, height, z) yields a
    # 150m-tall slab, and verify_level.py will not catch that: both files agree
    # perfectly, they are just both wrong. It checks that the two exports match,
    # not that the author meant what they built.
    floor_half = (extent, extent, 1.0)
    floor = add_mesh(collision, "BOX_Floor", box_verts(floor_half), BOX_FACES, MOSS)
    floor.location = to_blender((0.0, -1.0, 0.0))
    floor.display_type = "WIRE"

    # --- Scattered blocks ----------------------------------------------------
    # Spread over the full extent rather than clustered, so per-cascade culling
    # has something to reject: with everything near the origin, a 16m cascade
    # around the player would contain the whole level and culling would look
    # free when it is not.
    placed = 0
    for i in range(blocks):
        x = random.uniform(-extent * 0.92, extent * 0.92)
        z = random.uniform(-extent * 0.92, extent * 0.92)
        # Keep the spawn area clear so the player does not start inside a block.
        if math.hypot(x, z) < 8.0:
            continue
        w = random.uniform(1.5, 5.0)
        d = random.uniform(1.5, 5.0)
        h = random.uniform(2.0, 9.0)
        yaw = random.uniform(0.0, 360.0)
        rgb = STONE if (i % 2 == 0) else SLATE
        material = stone_mat if (i % 2 == 0) else slate_mat

        half_b = (w * 0.5, d * 0.5, h * 0.5)
        location = to_blender((x, h * 0.5, z))
        rotation = (0.0, 0.0, math.radians(yaw))

        proxy = add_mesh(collision, "BOX_Block%03d" % i,
                         box_verts(half_b), BOX_FACES, rgb)
        proxy.location = location
        proxy.rotation_euler = rotation
        proxy.display_type = "WIRE"

        solid = add_mesh(visual, "VIS_Block%03d" % i,
                         box_verts(half_b), BOX_FACES, rgb, material)
        solid.location = location
        solid.rotation_euler = rotation
        placed += 1

    # --- Markers -------------------------------------------------------------
    spawn = bpy.data.objects.new("PLAYER_SPAWN", None)
    spawn.empty_display_type = "SINGLE_ARROW"
    spawn.location = to_blender((0.0, 0.0, 0.0))
    markers.objects.link(spawn)

    for i, offset in enumerate([(6.0, 0.0, 6.0), (-6.0, 0.0, 6.0), (6.0, 0.0, -6.0)]):
        marker = bpy.data.objects.new("ENEMY_Swordman_%02d" % (i + 1), None)
        marker.empty_display_type = "PLAIN_AXES"
        marker.location = to_blender(offset)
        markers.objects.link(marker)

    return placed, grid * grid * 2


def parse_args(argv):
    argv = argv[argv.index("--") + 1:] if "--" in argv else []
    parser = argparse.ArgumentParser(prog="make_stress_level.py")
    parser.add_argument("--out", default="source/levels/stress.blend")
    parser.add_argument("--extent", type=float, default=75.0,
                        help="half-width in metres (default 75 -> a 150m map)")
    parser.add_argument("--grid", type=int, default=300,
                        help="ground subdivisions per side (300 -> 180k triangles)")
    parser.add_argument("--blocks", type=int, default=200)
    return parser.parse_args(argv)


def main():
    args = parse_args(sys.argv)
    out = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)

    placed, ground_tris = build(args.extent, args.grid, args.blocks)
    bpy.ops.wm.save_as_mainfile(filepath=out)

    print("[make_stress] wrote %s" % out)
    print("[make_stress]   %.0fm map, %d blocks, ~%d triangles "
          "(%d ground + %d blocks)"
          % (args.extent * 2, placed, ground_tris + placed * 12,
             ground_tris, placed * 12))


if __name__ == "__main__":
    main()
