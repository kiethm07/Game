"""Generate the greybox level .blend from the arena that used to be hardcoded.

The nine obstacles and four spawns below are transcribed from the constructor
of GameplayState as it stood before levels were data-driven
(GamePlayState.cpp:24-48). Rebuilding exactly that arena in Blender makes the
migration verifiable by A/B: the game should play identically after loading it
from JSON, and anything that moves is the coordinate conversion being wrong.

It also does the job a hand-authored .blend cannot do on its own -- every
collision proxy gets a matching solid in VISUAL, so the debug overlay's
wireframes and the exported mesh have to coincide. The two arrive by completely
different routes (this script's conversion for the JSON, Blender's glTF
exporter for the mesh), which is what makes the overlay a real test rather than
a round-trip that would agree with itself even when both sides are wrong.

Usage:
    blender --background --python tools/make_greybox_level.py -- \
        [--out source/levels/greybox.blend]

Then export it:
    blender --background source/levels/greybox.blend \
        --python tools/export_level.py -- --out-dir assets/levels/greybox

Axis note: the arena data below is in GAME space (Y-up). Blender is Z-up, so
`to_blender` applies the inverse of export_level.py's `to_game`.
"""

import argparse
import math
import os
import sys

import bpy

# --- The arena, verbatim from the old GameplayState constructor -------------
# Colours are raylib's, matching the Color constants the C++ passed.

DARKBLUE = (0, 82, 172)
SKYBLUE = (102, 191, 255)
GRAY = (130, 130, 130)
DARKGRAY = (80, 80, 80)
ORANGE = (255, 161, 0)
LIME = (0, 158, 47)
BEIGE = (211, 176, 131)

# (name, min_corner, max_corner, colour, yaw_degrees)
#
# The yawed wall is not part of the original arena. It is there so the export
# has something whose orientation actually matters: every other proxy here is
# axis-aligned, and an axis-aligned box looks identical under yaw and -yaw from
# every angle a bounding box can see. A long, thin, 30-degree wall is what makes
# tools/verify_level.py able to fail when the rotation sign is wrong.
BOXES = [
    ("BOX_CentralHub", (-4.0, 0.0, -4.0), (4.0, 2.5, 4.0), DARKBLUE, 0.0),
    ("BOX_SouthWall", (-4.0, 0.0, 13.0), (4.0, 4.0, 14.0), GRAY, 0.0),
    ("BOX_WestPillar", (-13.0, 0.0, -5.0), (-10.0, 6.0, -2.0), DARKGRAY, 0.0),
    ("BOX_CornerPlatform", (10.0, 0.0, -15.0), (15.0, 2.0, -10.0), ORANGE, 0.0),
    ("BOX_YawedWall", (-20.0, 0.0, 4.0), (-8.0, 3.0, 5.0), GRAY, 30.0),
    # New: the floor the level now owns. It used to be three separate,
    # uncoordinated things -- a decorative DrawPlane in the renderer, a
    # fabricated 200x200 plate fed only to the navmesh, and a hard y=0 clamp in
    # PhysicsManager. This proxy is the one the first two are replaced by.
    # 2m of thickness rather than a sliver so nothing can tunnel through it.
    ("BOX_Floor", (-40.0, -2.0, -40.0), (40.0, 0.0, 40.0), BEIGE, 0.0),
]

# (name, min_xz, max_xz, start_y, end_y, colour)
RAMPS = [
    ("RAMP_North", (-2.0, -11.0), (2.0, -4.0), 0.0, 2.5, SKYBLUE),
    ("RAMP_South", (-2.0, 4.0), (2.0, 11.0), 2.5, 0.0, SKYBLUE),
    ("RAMP_West", (-11.0, -2.0), (-4.0, 2.0), 0.0, 2.5, SKYBLUE),
    ("RAMP_East", (4.0, -2.0), (11.0, 2.0), 2.5, 0.0, SKYBLUE),
    ("RAMP_Corner", (10.0, -10.0), (15.0, -5.0), 2.0, 0.0, LIME),
]

PLAYER_SPAWN = (0.0, 0.0, -13.0)

# (name, position)
ENEMY_SPAWNS = [
    ("ENEMY_Swordman_01", (8.0, 0.0, 8.0)),
    ("ENEMY_Swordman_02", (-8.0, 0.0, 8.0)),
    ("ENEMY_Swordman_03", (8.0, 0.0, -8.0)),
]


def to_blender(p):
    """Game world space (Y-up) -> Blender world space (Z-up).

    The exact inverse of export_level.py's to_game, which sends Blender
    (x, y, z) to (x, z, -y).
    """
    gx, gy, gz = p
    return (gx, -gz, gy)


# ---------------------------------------------------------------------------
# Scene construction
# ---------------------------------------------------------------------------

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
    """A flat material carrying the proxy's colour as glTF baseColorFactor.

    No texture: the greybox exists to validate geometry and the conversion, and
    an untextured material exercises the loader's no-albedo path, which the real
    kitbashed map will not.
    """
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    bsdf = material.node_tree.nodes.get("Principled BSDF")
    if bsdf is not None:
        # Written straight through with no sRGB->linear conversion, which is
        # wrong colorimetrically and right for this engine. glTF baseColorFactor
        # is linear and Blender's colour inputs are linear, so converting would
        # be correct in isolation -- but raylib hands the factor to the shader
        # as-is and writes the result to a non-sRGB framebuffer, exactly as it
        # does with the sRGB bytes of a PNG albedo. Everything downstream lives
        # in sRGB space, so the factor has to as well, or the greybox comes out
        # visibly darker than the arena it is reproducing and reads as a
        # lighting bug.
        bsdf.inputs["Base Color"].default_value = (*[c / 255.0 for c in rgb], 1.0)
        if "Roughness" in bsdf.inputs:
            bsdf.inputs["Roughness"].default_value = 0.9
        if "Metallic" in bsdf.inputs:
            bsdf.inputs["Metallic"].default_value = 0.0
    return material


def add_mesh(collection, name, verts, faces, rgb, material=None):
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    # Viewport display colour: export_level.py reads this back as the debug
    # wireframe colour, so the in-game overlay matches what you see in Blender.
    obj.color = (*[c / 255.0 for c in rgb], 1.0)
    if material is not None:
        mesh.materials.append(material)
    collection.objects.link(obj)
    return obj


def box_verts(half):
    hx, hy, hz = half
    return [(-hx, -hy, -hz), (hx, -hy, -hz), (hx, hy, -hz), (-hx, hy, -hz),
            (-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz)]


BOX_FACES = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
             (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]


def ramp_corners_game(min_xz, max_xz, start_y, end_y):
    """The ramp's four top corners in game space.

    Height runs along whichever footprint axis is longer, because that is the
    axis PhysicsObstacle::getHeightAt interpolates over -- start_y at the low
    end of it, end_y at the high end.
    """
    min_x, min_z = min_xz
    max_x, max_z = max_xz
    slopes_z = abs(max_z - min_z) >= abs(max_x - min_x)

    def height(x, z):
        return start_y if (z == min_z if slopes_z else x == min_x) else end_y

    return [(min_x, height(min_x, min_z), min_z),
            (max_x, height(max_x, min_z), min_z),
            (max_x, height(max_x, max_z), max_z),
            (min_x, height(min_x, max_z), max_z)]


def build():
    reset_scene()
    visual = make_collection("VISUAL")
    collision = make_collection("COLLISION")
    markers = make_collection("MARKERS")

    # Proxies are for authoring, not for rendering. Hiding the collection also
    # stops it leaking into the glTF export.
    collision.hide_render = True
    collision.hide_viewport = False
    markers.hide_render = True

    for name, min_corner, max_corner, rgb, yaw in BOXES:
        centre_g = tuple((a + b) * 0.5 for a, b in zip(min_corner, max_corner))
        half_g = tuple((b - a) * 0.5 for a, b in zip(min_corner, max_corner))
        # Half-extents ride the same axis swap as positions, unsigned.
        half_b = (half_g[0], half_g[2], half_g[1])

        # Yaw is set as a Blender Z rotation, the one rotation the exporter
        # accepts -- it is read back out of matrix_world, not assumed.
        yaw_rad = math.radians(yaw)

        proxy = add_mesh(collision, name, box_verts(half_b), BOX_FACES, rgb)
        proxy.location = to_blender(centre_g)
        proxy.rotation_euler = (0.0, 0.0, yaw_rad)
        proxy.display_type = "WIRE"

        solid = add_mesh(visual, name.replace("BOX_", "VIS_"),
                         box_verts(half_b), BOX_FACES, rgb,
                         make_material("MAT_" + name, rgb))
        solid.location = proxy.location
        solid.rotation_euler = proxy.rotation_euler

    for name, min_xz, max_xz, start_y, end_y, rgb in RAMPS:
        corners_g = ramp_corners_game(min_xz, max_xz, start_y, end_y)
        corners_b = [to_blender(c) for c in corners_g]

        # The collision proxy is the bare quad export_level.py expects: exactly
        # four vertices, from which it re-derives yaw, footprint and end heights.
        proxy = add_mesh(collision, name, corners_b, [(0, 1, 2, 3)], rgb)
        proxy.display_type = "WIRE"

        # The visual is the solid wedge the player actually sees. Its underside
        # sits flat at the lower of the two ends, matching how
        # PhysicsObstacle::rampCorners builds the drawn volume.
        floor_y = min(start_y, end_y)
        bottom_b = [to_blender((c[0], floor_y, c[2])) for c in corners_g]
        wedge_verts = corners_b + bottom_b
        wedge_faces = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
                       (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
        add_mesh(visual, name.replace("RAMP_", "VIS_"), wedge_verts,
                 wedge_faces, rgb, make_material("MAT_" + name, rgb))

    spawn = bpy.data.objects.new(PLAYER_SPAWN_NAME, None)
    spawn.empty_display_type = "SINGLE_ARROW"
    spawn.empty_display_size = 1.8
    spawn.location = to_blender(PLAYER_SPAWN)
    markers.objects.link(spawn)

    for name, position in ENEMY_SPAWNS:
        marker = bpy.data.objects.new(name, None)
        marker.empty_display_type = "PLAIN_AXES"
        marker.empty_display_size = 1.0
        marker.location = to_blender(position)
        markers.objects.link(marker)


PLAYER_SPAWN_NAME = "PLAYER_SPAWN"


def parse_args(argv):
    argv = argv[argv.index("--") + 1:] if "--" in argv else []
    parser = argparse.ArgumentParser(prog="make_greybox_level.py")
    parser.add_argument("--out", default="source/levels/greybox.blend")
    return parser.parse_args(argv)


def main():
    args = parse_args(sys.argv)
    out = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)

    build()
    bpy.ops.wm.save_as_mainfile(filepath=out)

    print("[make_greybox] wrote %s" % out)
    print("[make_greybox]   %d boxes, %d ramps, %d enemy spawns"
          % (len(BOXES), len(RAMPS), len(ENEMY_SPAWNS)))


if __name__ == "__main__":
    main()
