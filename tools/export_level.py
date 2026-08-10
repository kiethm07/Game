"""Export a level .blend into the pair of files the game loads.

A level is authored once in Blender and leaves as two artefacts that must agree
with each other:

    VISUAL     collection  --> level.glb   the kitbashed mesh the player sees
    COLLISION  collection  --> level.json  PhysicsObstacle proxies
    MARKERS    collection  --/             player and enemy spawns

Both are written by this one script on purpose. Exporting the mesh by hand from
the GUI and the JSON from a script is how the two silently drift apart: you move
a wall, re-export one of them, and the collision is now somewhere the player
cannot see.

Usage:
    blender --background <level.blend> --python tools/export_level.py -- \
        --out-dir assets/levels/<name> [--name <name>] [--no-glb]

Authoring rules, all enforced below with a named-object error rather than a
silently wrong export:

  COLLISION/BOX_*    a cube. Yaw (rotation about Blender Z) only -- the engine's
                     PhysicsObstacle has no pitch or roll. Any X/Y rotation is
                     rejected.
  COLLISION/RAMP_*   a four-corner quad. Its plan-view footprint must be a
                     rectangle, and it must be at least as long along the slope
                     as it is wide across it (see the ramp section for why).
  MARKERS/PLAYER_SPAWN         required, exactly one.
  MARKERS/ENEMY_<Type>[_*]     zero or more; <Type> must name an EnemyType.

Axis note -- this is the part that breaks alignment when it is wrong. Blender is
Z-up, glTF is Y-up, and Blender's glTF exporter resolves that by mapping

    (x, y, z)_blender  ->  (x, z, -y)_gltf

`to_game` below applies exactly that same mapping to the collision proxies. The
two conversions have to be identical, or the JSON boxes end up somewhere other
than the mesh they are meant to be wrapping. Round-tripping proves nothing here
(a mistake made consistently in both directions still round-trips) -- what
proves it is drawing the proxy wireframes over the exported mesh in-game, which
is what the level debug overlay is for.
"""

import argparse
import json
import math
import os
import struct
import sys

import bpy
from mathutils import Vector

# Collection names. Case-sensitive, and all three are looked up by exact name so
# a typo is an error rather than a quietly empty export.
VISUAL_COLLECTION = "VISUAL"
COLLISION_COLLECTION = "COLLISION"
MARKERS_COLLECTION = "MARKERS"

# Optional, and the only one of the four that is: a level without it collides
# purely against BOX_/RAMP_ proxies, which is what greybox and forest do. When
# present it carries the geometry those primitives cannot express -- curved
# ground, round towers, arches -- as a triangle soup the engine loads into a
# BVH (include/Physics/CollisionMesh.h).
COLLISION_MESH_COLLECTION = "COLLISION_MESH"

# Bumped only for levels that actually ship a collision mesh. A build that
# predates the mesh would read one of those as a level whose ground is simply
# missing -- collision that loads *wrongly* rather than incompletely, which is
# the case LevelLoader's version check exists to refuse. Levels without a mesh
# keep writing format 1 and stay byte-for-byte what they were.
FORMAT_WITHOUT_MESH = 1
FORMAT_WITH_MESH = 2

BOX_PREFIX = "BOX_"
RAMP_PREFIX = "RAMP_"
PLAYER_SPAWN = "PLAYER_SPAWN"
ENEMY_PREFIX = "ENEMY_"

# Must stay in step with EnemyType in include/Entities/EnemyFactory.h. Kept as a
# list here so a marker naming a type the factory cannot build fails at export
# rather than at runtime.
ENEMY_TYPES = ["Swordman"]

# Two proxies whose faces are flush would leave the character's depenetration
# pass arbitrating between them; more practically, a zero-thickness box is
# almost always a modelling slip rather than intent.
MIN_EXTENT = 0.01

# How square a ramp's plan-view footprint is allowed to be. PhysicsObstacle
# interpolates a ramp's height along whichever footprint axis is LONGER
# (getHeightAt, PhysicsObstacle.cpp:114-127), not along the axis the slope
# actually runs. The two coincide only while the ramp is longer than it is wide,
# so that is checked rather than assumed.
RAMP_ASPECT_EPSILON = 1e-3


class ExportError(Exception):
    """Authoring mistake, reported with the object that caused it."""


# ---------------------------------------------------------------------------
# Coordinate conversion
# ---------------------------------------------------------------------------

def to_game(v):
    """Blender world space -> game world space. Mirrors the glTF exporter."""
    return (v.x, v.z, -v.y)


def yaw_degrees(obj):
    """Blender's Z rotation, in degrees, as the engine's Y yaw.

    No sign flip. Blender local +X under a Z-rotation t is (cos t, sin t, 0),
    which to_game sends to (cos t, 0, -sin t); raymath's MatrixRotateY(t) sends
    game +X to (cos t, 0, -sin t) as well. The conventions already agree.
    """
    return math.degrees(obj.matrix_world.to_euler("XYZ").z)


def game_bounds(points):
    """Axis-aligned min/max of game-space points, as two 3-lists."""
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    zs = [p[2] for p in points]
    return [min(xs), min(ys), min(zs)], [max(xs), max(ys), max(zs)]


def world_corners(obj):
    """The object's eight bounding-box corners in Blender world space."""
    return [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]


def display_color(obj):
    """Blender's viewport display colour as an 8-bit RGBA quad.

    This is what the engine's debug wireframe overlay draws proxies in, so
    colour-coding ramps against walls in Blender carries through to the game.
    """
    return [max(0, min(255, int(round(c * 255.0)))) for c in obj.color]


# ---------------------------------------------------------------------------
# Boxes
# ---------------------------------------------------------------------------

def export_box(obj):
    euler = obj.matrix_world.to_euler("XYZ")
    for axis, angle in (("X", euler.x), ("Y", euler.y)):
        # Fold into [-pi, pi] first: a cube rotated 180 degrees twice reads as
        # 2pi, which is no rotation at all but fails a naive near-zero test.
        folded = (angle + math.pi) % (2.0 * math.pi) - math.pi
        if abs(folded) > 1e-4:
            raise ExportError(
                "%s is rotated %.1f degrees about %s. PhysicsObstacle supports "
                "yaw (Blender Z) only -- rotate about Z, or model the shape in "
                "the VISUAL collection and wrap it in axis-aligned boxes here."
                % (obj.name, math.degrees(folded), axis))

    # Local bounding box scaled by the object's own scale. Read from bound_box
    # rather than obj.dimensions so the result does not depend on how fresh the
    # depsgraph is in a --background run.
    corners = [Vector(c) for c in obj.bound_box]
    scale = obj.matrix_world.to_scale()
    size = Vector((
        (max(c.x for c in corners) - min(c.x for c in corners)) * abs(scale.x),
        (max(c.y for c in corners) - min(c.y for c in corners)) * abs(scale.y),
        (max(c.z for c in corners) - min(c.z for c in corners)) * abs(scale.z),
    ))

    if min(size) < MIN_EXTENT:
        raise ExportError(
            "%s has a near-zero extent (%.4f x %.4f x %.4f). Give it real "
            "thickness -- a flat box is not a wall." % (obj.name, *size))

    centre = to_game(obj.matrix_world.translation)
    # Half-extents ride the same axis swap as positions, but unsigned: the -y
    # of to_game only flips direction, and an extent has no direction.
    half = (size.x * 0.5, size.z * 0.5, size.y * 0.5)

    return {
        "type": "box",
        "name": obj.name,
        "center": [round(c, 5) for c in centre],
        "halfExtents": [round(h, 5) for h in half],
        "yaw": round(yaw_degrees(obj), 5),
        "color": display_color(obj),
    }


# ---------------------------------------------------------------------------
# Ramps
# ---------------------------------------------------------------------------

def export_ramp(obj):
    mesh = obj.data
    if mesh is None or not hasattr(mesh, "vertices"):
        raise ExportError("%s is not a mesh; a ramp must be a four-corner quad."
                          % obj.name)

    corners = [to_game(obj.matrix_world @ v.co) for v in mesh.vertices]
    if len(corners) != 4:
        raise ExportError(
            "%s has %d vertices; a ramp must be exactly four corners. Model "
            "the visible ramp in VISUAL and put a clean quad here."
            % (obj.name, len(corners)))

    centre_x = sum(c[0] for c in corners) / 4.0
    centre_z = sum(c[2] for c in corners) / 4.0
    centre_y = sum(c[1] for c in corners) / 4.0

    rise = max(c[1] for c in corners) - min(c[1] for c in corners)
    if abs(rise) < MIN_EXTENT:
        raise ExportError(
            "%s is flat (rise %.4f). A ramp needs two different heights -- use "
            "a BOX_ proxy for a flat platform." % (obj.name, rise))

    # The slope direction is the gradient of the plane through the four
    # corners: fit y = a*x + b*z about the centroid and read off (a, b). Not
    # taken from a pair of corners, which is only right by luck -- on a square
    # footprint the lowest and highest corner sit diagonally opposite, and the
    # diagonal is 45 degrees away from the direction the ramp actually climbs.
    s_xx = s_xz = s_zz = s_xy = s_zy = 0.0
    for cx, cy, cz in corners:
        dx, dy, dz = cx - centre_x, cy - centre_y, cz - centre_z
        s_xx += dx * dx
        s_xz += dx * dz
        s_zz += dz * dz
        s_xy += dx * dy
        s_zy += dz * dy

    det = s_xx * s_zz - s_xz * s_xz
    if abs(det) < 1e-9:
        raise ExportError(
            "%s has no footprint in plan view -- its corners are colinear from "
            "above. That is a wall, not a ramp; use a BOX_ proxy." % obj.name)

    uphill_x = (s_xy * s_zz - s_zy * s_xz) / det
    uphill_z = (s_zy * s_xx - s_xy * s_xz) / det
    uphill_len = math.hypot(uphill_x, uphill_z)
    if uphill_len < 1e-6:
        raise ExportError(
            "%s rises vertically with no horizontal run. That is a wall, not a "
            "ramp -- use a BOX_ proxy." % obj.name)

    # Yaw that carries local +Z onto the uphill direction. MatrixRotateY(t)
    # sends local +Z to (sin t, 0, cos t), hence atan2(x, z) rather than the
    # usual atan2(z, x).
    yaw = math.atan2(uphill_x / uphill_len, uphill_z / uphill_len)

    # De-yaw the footprint. PhysicsObstacle's ramp constructor takes an
    # axis-aligned footprint that it then spins about its own centre, so what
    # goes in the JSON is the rectangle as measured in the unrotated frame.
    cos_y, sin_y = math.cos(-yaw), math.sin(-yaw)
    local = []
    for cx, cy, cz in corners:
        dx, dz = cx - centre_x, cz - centre_z
        # Inverse of the same convention: x' = x cos + z sin, z' = -x sin + z cos.
        local.append((dx * cos_y + dz * sin_y, cy, -dx * sin_y + dz * cos_y))

    min_x = min(p[0] for p in local)
    max_x = max(p[0] for p in local)
    min_z = min(p[2] for p in local)
    max_z = max(p[2] for p in local)

    # A rectangle in plan view: every corner sits on the footprint boundary.
    for point, original in zip(local, corners):
        on_x = min(abs(point[0] - min_x), abs(point[0] - max_x))
        on_z = min(abs(point[2] - min_z), abs(point[2] - max_z))
        if on_x > 1e-3 or on_z > 1e-3:
            raise ExportError(
                "%s is not a rectangle in plan view -- corner (%.3f, %.3f, "
                "%.3f) sits inside the footprint. A ramp must be a rectangular "
                "quad, sloped along one axis only; a twisted or trapezoidal "
                "quad has no PhysicsObstacle equivalent."
                % (obj.name, *original))

    length = max_z - min_z   # along the slope, by construction of yaw
    width = max_x - min_x    # across it
    if length + RAMP_ASPECT_EPSILON < width:
        raise ExportError(
            "%s is %.2fm wide but only %.2fm long along its slope. "
            "PhysicsObstacle interpolates a ramp's height along its LONGER "
            "footprint axis, so this one would slope sideways in-game. Make it "
            "longer than it is wide, or split it into several narrower ramps."
            % (obj.name, width, length))

    # Heights at the two ends of the slope axis, read off the de-yawed corners.
    start_y = sum(p[1] for p in local if abs(p[2] - min_z) <= 1e-3)
    start_n = sum(1 for p in local if abs(p[2] - min_z) <= 1e-3)
    end_y = sum(p[1] for p in local if abs(p[2] - max_z) <= 1e-3)
    end_n = sum(1 for p in local if abs(p[2] - max_z) <= 1e-3)
    if start_n == 0 or end_n == 0:
        raise ExportError("%s: could not identify the ramp's two ends."
                          % obj.name)
    start_y /= start_n
    end_y /= end_n

    return {
        "type": "ramp",
        "name": obj.name,
        # Expressed about the footprint centre, which is what the constructor
        # re-derives as the pivot the yaw is applied about.
        "minXZ": [round(centre_x + min_x, 5), round(centre_z + min_z, 5)],
        "maxXZ": [round(centre_x + max_x, 5), round(centre_z + max_z, 5)],
        "startY": round(start_y, 5),
        "endY": round(end_y, 5),
        "yaw": round(math.degrees(yaw), 5),
        "color": display_color(obj),
    }


# ---------------------------------------------------------------------------
# Markers
# ---------------------------------------------------------------------------

def export_markers(collection):
    player = None
    enemies = []

    for obj in sorted(collection.all_objects, key=lambda o: o.name):
        position = [round(c, 5) for c in to_game(obj.matrix_world.translation)]
        yaw = round(yaw_degrees(obj), 5)

        if obj.name == PLAYER_SPAWN:
            if player is not None:
                raise ExportError("more than one %s in %s"
                                  % (PLAYER_SPAWN, collection.name))
            player = {"position": position, "yaw": yaw}

        elif obj.name.startswith(ENEMY_PREFIX):
            # ENEMY_Swordman_03 -> "Swordman". The trailing index is there to
            # keep Blender names unique and carries no meaning.
            rest = obj.name[len(ENEMY_PREFIX):]
            kind = rest.split("_")[0]
            if kind not in ENEMY_TYPES:
                raise ExportError(
                    "%s names enemy type %r, which is not in EnemyType. Known "
                    "types: %s" % (obj.name, kind, ", ".join(ENEMY_TYPES)))
            enemies.append({"type": kind, "position": position, "yaw": yaw})

        else:
            raise ExportError(
                "%s in %s is not a recognised marker. Expected %s or %s<Type>."
                % (obj.name, collection.name, PLAYER_SPAWN, ENEMY_PREFIX))

    if player is None:
        raise ExportError("no %s marker in %s -- the level has nowhere to put "
                          "the player." % (PLAYER_SPAWN, MARKERS_COLLECTION))

    return player, enemies


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------

def require_collection(name):
    collection = bpy.data.collections.get(name)
    if collection is None:
        raise ExportError(
            "the .blend has no collection named %r. A level needs %s, %s and "
            "%s." % (name, VISUAL_COLLECTION, COLLISION_COLLECTION,
                     MARKERS_COLLECTION))
    return collection


def export_collision(collection):
    obstacles = []
    corners = []

    for obj in sorted(collection.all_objects, key=lambda o: o.name):
        if obj.name.startswith(BOX_PREFIX):
            obstacles.append(export_box(obj))
        elif obj.name.startswith(RAMP_PREFIX):
            obstacles.append(export_ramp(obj))
        else:
            raise ExportError(
                "%s in %s is named neither %s* nor %s*. Every collision proxy "
                "has to declare which shape it is."
                % (obj.name, collection.name, BOX_PREFIX, RAMP_PREFIX))
        corners.extend(to_game(c) for c in world_corners(obj))

    if not obstacles:
        raise ExportError("%s is empty -- the level has no collision at all."
                          % collection.name)
    return obstacles, corners


def export_glb(collection, path):
    """Write the VISUAL collection to a .glb raylib can actually load.

    Draco and KTX2/basisu are both left off deliberately: raylib's loader is
    cgltf, which supports neither, and a model using either arrives with no
    geometry rather than with an error.

    AUTO leaves the PNG/JPEG choice to Blender, which picks JPEG whenever a
    texture has no alpha worth keeping. raylib only decodes those because the
    build defines SUPPORT_FILEFORMAT_JPG (CMakeLists.txt) -- its config.h
    defaults that off, and without it a textured level loads untextured with
    only an "IMAGE: Data format not supported" warning to say why.
    """
    bpy.ops.object.select_all(action="DESELECT")
    for obj in collection.all_objects:
        obj.select_set(True)

    bpy.ops.export_scene.gltf(
        filepath=path,
        export_format="GLB",
        use_selection=True,
        export_yup=True,
        export_apply=True,          # resolve modifiers
        export_draco_mesh_compression_enable=False,
        export_image_format="AUTO", # embedded PNG/JPEG, never KTX2
        export_cameras=False,
        export_lights=False,
        export_animations=False,
        export_skins=False,
    )


def export_collision_mesh(collection, path):
    """Write the COLLISION_MESH collection as a flat triangle soup.

    Deliberately NOT a .glb, unlike the visual mesh, for two reasons.

    The disqualifying one: raylib's Mesh stores indices as `unsigned short`
    (raylib.h:355), so a mesh cannot address more than 65,535 vertices. The
    castle's collision geometry is a single 89,725-triangle primitive with
    67,406 vertices and 32-bit indices, and loading that through raylib would
    truncate every index to 16 bits -- not fail, truncate. The result is a mesh
    whose triangles connect the wrong vertices, which as collision means falling
    through the world at random. That is precisely the class of silent glTF
    breakage raylib's loader is already known for.

    The better one: this applies `to_game` itself, the same function the BOX_
    and RAMP_ proxies go through. The visual mesh relies on Blender's exporter
    performing the identical axis swap, which is a correspondence that has to be
    maintained by hand; here the collision surface and the collision proxies
    cannot drift apart, because they are converted by the same code.

    Layout, little-endian throughout:
        magic  "SKCM"            4 bytes
        u32    version           1
        u32    vertex_count
        u32    triangle_count
        f32    positions[vertex_count * 3]    game space
        u32    indices[triangle_count * 3]
    """
    depsgraph = bpy.context.evaluated_depsgraph_get()
    positions = []
    indices = []

    for obj in sorted(collection.all_objects, key=lambda o: o.name):
        if obj.type != "MESH":
            continue
        evaluated = obj.evaluated_get(depsgraph)
        mesh = evaluated.to_mesh()
        if mesh is None:
            continue
        try:
            mesh.calc_loop_triangles()
            base = len(positions) // 3
            matrix = obj.matrix_world
            for vertex in mesh.vertices:
                x, y, z = to_game(matrix @ vertex.co)
                positions.extend((x, y, z))
            for tri in mesh.loop_triangles:
                # Winding is copied through unchanged, and that is worth being
                # precise about because it looks like it should not be.
                # `to_game` negates a component, which suggests a mirror -- but
                # (x, y, z) -> (x, z, -y) as a matrix is
                #     [[1, 0, 0], [0, 0, 1], [0, -1, 0]]
                # whose determinant is +1. It is a -90 degree rotation about X,
                # not a reflection, so handedness is preserved and the source
                # winding is already correct in game space.
                #
                # Swapping two corners "to compensate" inverts every normal
                # instead. PhysicsManager tells a floor from a ceiling by the
                # sign of normal.y (classifySurfaceNormal), so that mistake
                # makes the entire world read as ceiling: flat ground comes back
                # with normal.y = -1.
                indices.extend(base + v for v in tri.vertices)
        finally:
            evaluated.to_mesh_clear()

    vertex_count = len(positions) // 3
    triangle_count = len(indices) // 3
    if triangle_count == 0:
        raise ExportError(
            "%s produced no triangles. A collision mesh that exports empty "
            "would ship a level with no ground." % collection.name)

    with open(path, "wb") as f:
        f.write(b"SKCM")
        f.write(struct.pack("<III", 1, vertex_count, triangle_count))
        f.write(struct.pack("<%df" % len(positions), *positions))
        f.write(struct.pack("<%dI" % len(indices), *indices))

    return vertex_count, triangle_count


def parse_args(argv):
    if "--" in argv:
        argv = argv[argv.index("--") + 1:]
    else:
        argv = []
    parser = argparse.ArgumentParser(prog="export_level.py")
    parser.add_argument("--out-dir", required=True,
                        help="directory to write level.json and level.glb into")
    parser.add_argument("--name", default=None,
                        help="level name recorded in the JSON "
                             "(defaults to the output directory's name)")
    parser.add_argument("--no-glb", action="store_true",
                        help="write only level.json, leaving the mesh alone")
    return parser.parse_args(argv)


def main():
    args = parse_args(sys.argv)
    out_dir = os.path.abspath(args.out_dir)
    os.makedirs(out_dir, exist_ok=True)
    name = args.name or os.path.basename(out_dir)

    visual = require_collection(VISUAL_COLLECTION)
    collision = require_collection(COLLISION_COLLECTION)
    markers = require_collection(MARKERS_COLLECTION)

    obstacles, collision_corners = export_collision(collision)
    player, enemies = export_markers(markers)

    # Shadow framing is driven off these bounds, so they cover what is drawn as
    # well as what is collided against -- a roof that casts but has no proxy
    # still has to sit inside the far cascade.
    corners = list(collision_corners)
    for obj in visual.all_objects:
        if obj.type == "MESH":
            corners.extend(to_game(c) for c in world_corners(obj))
    bounds_min, bounds_max = game_bounds(corners)

    glb_name = "level.glb"
    if not args.no_glb:
        export_glb(visual, os.path.join(out_dir, glb_name))

    collision_mesh = bpy.data.collections.get(COLLISION_MESH_COLLECTION)
    if collision_mesh is not None and not any(
            o.type == "MESH" for o in collision_mesh.all_objects):
        # An empty collection is an authoring slip, not a level that opted out:
        # the level would ship claiming a collision mesh and load with none.
        raise ExportError(
            "%s exists but holds no mesh. Delete the collection to export a "
            "proxy-only level, or put the collision geometry in it."
            % COLLISION_MESH_COLLECTION)

    mesh_name = None
    mesh_stats = None
    if collision_mesh is not None:
        mesh_name = "collision.bin"
        mesh_stats = export_collision_mesh(collision_mesh,
                                           os.path.join(out_dir, mesh_name))

    level = {
        "format": FORMAT_WITH_MESH if mesh_name else FORMAT_WITHOUT_MESH,
        "name": name,
        "visualModel": glb_name,
        "bounds": {"min": [round(c, 5) for c in bounds_min],
                   "max": [round(c, 5) for c in bounds_max]},
        "playerSpawn": player,
        "enemySpawns": enemies,
        "obstacles": obstacles,
    }
    # Placed after `obstacles` on purpose: a level without a mesh then produces
    # exactly the JSON it produced before this key existed, which is what makes
    # the forest byte-comparison a usable regression test.
    if mesh_name:
        level["collisionMesh"] = mesh_name

    json_path = os.path.join(out_dir, "level.json")
    with open(json_path, "w") as f:
        json.dump(level, f, indent=2)
        f.write("\n")

    boxes = sum(1 for o in obstacles if o["type"] == "box")
    ramps = len(obstacles) - boxes
    print("[export_level] %s -> %s" % (name, out_dir))
    print("[export_level]   %d boxes, %d ramps, %d enemy spawns"
          % (boxes, ramps, len(enemies)))
    if mesh_name:
        print("[export_level]   collision mesh -> %s (%d verts, %d tris), "
              "format %d" % (mesh_name, mesh_stats[0], mesh_stats[1],
                             FORMAT_WITH_MESH))
    print("[export_level]   bounds min=%s max=%s"
          % (["%.1f" % c for c in bounds_min], ["%.1f" % c for c in bounds_max]))


if __name__ == "__main__":
    try:
        main()
    except ExportError as err:
        # Blender swallows a plain traceback's exit code in --background, so the
        # failure is made loud here and the process is killed with a non-zero
        # status the shell can see.
        print("\n[export_level] EXPORT FAILED: %s\n" % err, file=sys.stderr)
        sys.exit(1)
