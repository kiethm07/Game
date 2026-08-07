"""Check that a level's collision proxies line up with its visual mesh.

This is the test the two-file export needs. level.json and level.glb are
produced from the same .blend but reach game space by completely different
routes -- the JSON through export_level.py's own `to_game`, the mesh through
Blender's glTF exporter -- and a level is only correct if those two conversions
agree. Round-tripping cannot show that: a mistake made consistently in both
directions round-trips perfectly.

Standalone: no Blender, no dependencies. Parses the GLB container directly, the
same way tools/inspect_root_motion.py does, and applies node transforms the way
raylib's loader does (it bakes them into the vertex data at load, which is why
the game draws the level at identity).

Usage:
    python3 tools/verify_level.py assets/levels/<name>

Two checks, in increasing strength:

  1. Bounds. The GLB's overall AABB against the `bounds` field in the JSON.
     Applies to every level. A mirrored layout or a swapped axis shows up here
     immediately.

  2. Per-proxy. When a visual mesh is named after a collision proxy
     (VIS_Foo <-> BOX_Foo / RAMP_Foo), the two are compared shape by shape.

     Not by AABB: an axis-aligned box's AABB is identical under yaw and -yaw, so
     a bounding-box comparison cannot see a flipped rotation sign at all. What
     is compared instead is the support function -- the furthest the shape
     reaches along each of sixteen directions around the compass. Two shapes
     with the same support values in every direction are the same convex shape
     in the same orientation, and the diagonal directions are exactly the ones a
     sign flip moves.

A kitbashed map will usually only satisfy check 1, since its art is not built
one-mesh-per-proxy. That is expected; check 2 reports what it could pair up.
"""

import json
import math
import os
import struct
import sys

COMPONENT = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2),
             5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}
COUNT = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}

# Tolerance in metres. The exporter rounds to 5 decimals and glTF stores floats,
# so agreement is exact to well within a millimetre when it is right; anything
# above this is a real disagreement, not accumulated error.
TOLERANCE = 0.002


def load_glb(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"glTF":
        raise SystemExit("%s is not a GLB" % path)

    offset, gltf, buffer = 12, None, None
    while offset < len(data):
        length, kind = struct.unpack_from("<II", data, offset)
        offset += 8
        chunk = data[offset:offset + length]
        offset += length
        if kind == 0x4E4F534A:
            gltf = json.loads(chunk)
        elif kind == 0x004E4942:
            buffer = chunk
    return gltf, buffer


def read_accessor(gltf, buffer, index):
    acc = gltf["accessors"][index]
    view = gltf["bufferViews"][acc["bufferView"]]
    fmt, size = COMPONENT[acc["componentType"]]
    n = COUNT[acc["type"]]
    base = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    stride = view.get("byteStride") or size * n
    return [struct.unpack_from("<" + fmt * n, buffer, base + i * stride)
            for i in range(acc["count"])]


# --- Matrices, column-major as glTF stores them -----------------------------

IDENTITY = (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)


def mat_multiply(a, b):
    """a then b, i.e. the transform b*a applied to a column vector."""
    out = [0.0] * 16
    for col in range(4):
        for row in range(4):
            out[col * 4 + row] = sum(
                b[k * 4 + row] * a[col * 4 + k] for k in range(4))
    return tuple(out)


def mat_from_node(node):
    if "matrix" in node:
        return tuple(node["matrix"])

    t = node.get("translation", [0.0, 0.0, 0.0])
    r = node.get("rotation", [0.0, 0.0, 0.0, 1.0])  # xyzw
    s = node.get("scale", [1.0, 1.0, 1.0])

    x, y, z, w = r
    rot = (
        1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0,
        2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0,
        2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0,
        0, 0, 0, 1,
    )
    # Scale each of the three basis columns; the fourth is translation and is
    # written in whole below.
    scaled = tuple(rot[col * 4 + row] * s[col] if row < 3 else 0.0
                   for col in range(3) for row in range(4))
    return scaled + (t[0], t[1], t[2], 1.0)


def transform_point(m, p):
    x, y, z = p
    return (m[0] * x + m[4] * y + m[8] * z + m[12],
            m[1] * x + m[5] * y + m[9] * z + m[13],
            m[2] * x + m[6] * y + m[10] * z + m[14])


# --- Mesh extraction --------------------------------------------------------

# Directions the support function is sampled along: sixteen evenly spaced
# compass headings at three elevations (level and +-45 degrees), plus straight
# up and down.
#
# Each part earns its place. The axis-aligned level directions reduce to the
# bounding box, so this strictly subsumes an AABB comparison. The level
# diagonals are what catch a flipped yaw on a box, whose AABB is identical under
# yaw and -yaw. The TILTED ones are what catch a flipped ramp: rotating a ramp
# by 180 degrees swaps its high end for its low end while leaving its footprint
# exactly where it was, so every purely horizontal direction reports the same
# value and only a direction with both height and heading can tell them apart.
def support_directions():
    dirs = [(0.0, 1.0, 0.0), (0.0, -1.0, 0.0)]
    for elevation in (0.0, math.pi / 4.0, -math.pi / 4.0):
        horizontal, vertical = math.cos(elevation), math.sin(elevation)
        for i in range(16):
            t = math.tau * i / 16.0
            dirs.append((math.cos(t) * horizontal, vertical,
                         math.sin(t) * horizontal))
    return dirs


DIRECTIONS = support_directions()


def support(points):
    """How far a point set reaches along each direction in DIRECTIONS."""
    return [max(p[0] * d[0] + p[1] * d[1] + p[2] * d[2] for p in points)
            for d in DIRECTIONS]


def mesh_bounds(gltf, buffer):
    """Support values per node name, plus the overall AABB, in game space.

    Node transforms are composed down the hierarchy and applied to the vertices,
    matching what raylib's loader does at load time.
    """
    per_node = {}
    overall = None

    def visit(index, parent):
        node = gltf["nodes"][index]
        world = mat_multiply(mat_from_node(node), parent)

        if "mesh" in node:
            points = []
            for prim in gltf["meshes"][node["mesh"]].get("primitives", []):
                position = prim.get("attributes", {}).get("POSITION")
                if position is None:
                    continue
                for v in read_accessor(gltf, buffer, position):
                    points.append(transform_point(world, v))
            if points:
                per_node[node.get("name", "mesh%d" % index)] = support(points)
                box = aabb(points)
                nonlocal overall
                overall = box if overall is None else union(overall, box)

        for child in node.get("children", []):
            visit(child, world)

    scene = gltf.get("scenes", [{}])[gltf.get("scene", 0)]
    for root in scene.get("nodes", []):
        visit(root, IDENTITY)

    return per_node, overall


def aabb(points):
    return ([min(p[i] for p in points) for i in range(3)],
            [max(p[i] for p in points) for i in range(3)])


def union(a, b):
    return ([min(a[0][i], b[0][i]) for i in range(3)],
            [max(a[1][i], b[1][i]) for i in range(3)])


# --- Obstacle AABBs from the JSON ------------------------------------------

def rotate_y(x, z, yaw_deg):
    """The same rotation raymath's MatrixRotateY applies."""
    t = math.radians(yaw_deg)
    c, s = math.cos(t), math.sin(t)
    return (x * c + z * s, -x * s + z * c)


def obstacle_corners(obs):
    """The obstacle's world-space corners, matching how PhysicsObstacle builds
    its drawn and collided volume."""
    yaw = obs.get("yaw", 0.0)

    if obs["type"] == "box":
        cx, cy, cz = obs["center"]
        hx, hy, hz = obs["halfExtents"]
        corners = []
        for sx in (-1, 1):
            for sy in (-1, 1):
                for sz in (-1, 1):
                    rx, rz = rotate_y(sx * hx, sz * hz, yaw)
                    corners.append((cx + rx, cy + sy * hy, cz + rz))
        return corners

    min_x, min_z = obs["minXZ"]
    max_x, max_z = obs["maxXZ"]
    start_y, end_y = obs["startY"], obs["endY"]
    cx, cz = (min_x + max_x) * 0.5, (min_z + max_z) * 0.5
    cy = (start_y + end_y) * 0.5

    # The drawn and collided volume: the sloped top, and a flat bottom at the
    # lower of the two ends (PhysicsObstacle::rampCorners).
    floor = min(start_y, end_y)
    slopes_z = abs(max_z - min_z) >= abs(max_x - min_x)

    corners = []
    for x in (min_x, max_x):
        for z in (min_z, max_z):
            top = start_y if ((z == min_z) if slopes_z else (x == min_x)) else end_y
            for y in (top, floor):
                rx, rz = rotate_y(x - cx, z - cz, yaw)
                corners.append((cx + rx, y, cz + rz))
    return corners


def format_box(box):
    return "min=(%.3f, %.3f, %.3f) max=(%.3f, %.3f, %.3f)" % (*box[0], *box[1])


def compare_boxes(label, expected, actual, failures):
    delta = max(max(abs(a - b) for a, b in zip(expected[i], actual[i]))
                for i in range(2))
    if delta <= TOLERANCE:
        print("  OK    %-24s  (max delta %.4fm)" % (label, delta))
        return
    failures.append(label)
    print("  FAIL  %-24s  max delta %.4fm" % (label, delta))
    print("          json: %s" % format_box(expected))
    print("          mesh: %s" % format_box(actual))


def compare_support(label, expected, actual, failures):
    worst = max(range(len(DIRECTIONS)),
                key=lambda i: abs(expected[i] - actual[i]))
    delta = abs(expected[worst] - actual[worst])
    if delta <= TOLERANCE:
        print("  OK    %-24s  (max delta %.4fm)" % (label, delta))
        return
    failures.append(label)
    d = DIRECTIONS[worst]
    print("  FAIL  %-24s  max delta %.4fm" % (label, delta))
    print("          along (%.2f, %.2f, %.2f): json reaches %.4f, mesh %.4f"
          % (*d, expected[worst], actual[worst]))
    if abs(d[1]) < 0.5 and abs(d[0]) > 0.01 and abs(d[2]) > 0.01:
        print("          (a diagonal direction — this is the signature of a "
              "wrong yaw sign)")


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: python3 tools/verify_level.py <level-dir>")

    level_dir = sys.argv[1]
    with open(os.path.join(level_dir, "level.json")) as f:
        level = json.load(f)

    glb_path = os.path.join(level_dir, level.get("visualModel", "level.glb"))
    if not os.path.exists(glb_path):
        raise SystemExit("%s has no visual mesh to check against" % level_dir)

    gltf, buffer = load_glb(glb_path)
    per_node, overall = mesh_bounds(gltf, buffer)
    if overall is None:
        raise SystemExit("%s contains no mesh geometry" % glb_path)

    failures = []

    print("%s\n" % level.get("name", level_dir))
    print("1. Overall bounds")
    declared = (level["bounds"]["min"], level["bounds"]["max"])
    # The JSON bounds cover collision proxies as well as visual meshes, so they
    # can legitimately be larger -- but never smaller, and never offset. Compare
    # against the union of both, which is what the exporter actually wrote.
    proxy_corners = [obstacle_corners(o) for o in level["obstacles"]]
    combined = overall
    for corners in proxy_corners:
        combined = union(combined, aabb(corners))
    compare_boxes("bounds", declared, combined, failures)

    print("\n2. Per-proxy alignment")
    paired = 0
    for obs, corners in zip(level["obstacles"], proxy_corners):
        name = obs.get("name", "")
        # BOX_Foo / RAMP_Foo <-> VIS_Foo
        stem = name.split("_", 1)[1] if "_" in name else name
        node = next((n for n in per_node if n == "VIS_" + stem), None)
        if node is None:
            continue
        paired += 1
        compare_support(name, support(corners), per_node[node], failures)

    if paired == 0:
        print("  (no VIS_* meshes named after a proxy — nothing to pair up.\n"
              "   Normal for a kitbashed map; check 1 above still applies.)")

    print()
    if failures:
        print("FAILED: %d of %d checks disagree.\n"
              "A uniform offset means the axis conversion is wrong; a rotation\n"
              "that only shows on yawed proxies means the yaw sign is."
              % (len(failures), paired + 1))
        return 1

    print("PASSED: collision proxies and visual mesh agree to within %.0fmm."
          % (TOLERANCE * 1000))
    return 0


if __name__ == "__main__":
    sys.exit(main())
