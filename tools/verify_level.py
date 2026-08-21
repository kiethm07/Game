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

Six checks, the first three in increasing strength:

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

  3. Collision mesh. Levels at format 2 also ship collision.bin, a triangle
     soup. Its winding is checked globally (up-facing triangles must outnumber
     down-facing ones) and every spawn marker is required to have mesh
     underneath it, at roughly its own height, facing up.

     The winding check exists because `to_game` is a rotation with determinant
     +1 and therefore preserves handedness -- which is unobvious enough that
     "compensating" by reversing triangle order is an easy mistake, and it
     inverts every normal in the level. PhysicsManager tells a floor from a
     ceiling by the sign of normal.y, so an inverted soup is a world with no
     floors, and checks 1 and 2 cannot see it.

  4. Detail mesh. Levels that ship a `detailModel` -- drawn-only scenery, grass
     today -- have it checked for the three faults that produce no runtime
     error at all: a primitive too big for raylib's 16-bit indices, a missing
     COLOR_0, and geometry outside the declared bounds. See check_detail_mesh
     for why each of those is silent.

  5. Enemy spawns. When the level carries an enemies.json overlay -- the
     hand-authored spawn list that replaces its Blender markers -- it is
     schema-checked, bounds-checked, and (where there is a collision mesh) the
     height the engine will snap each `y`-less entry to is resolved and
     printed. Note check 3 tests whichever spawn list the GAME will use, so an
     overlay repoints it.

  6. Tree proxies. Reports collision boxes the player can neither reach nor be
     hidden behind — wasted per-frame work that a re-export silently restores.
     A note, never a failure: the level plays identically either way.

A kitbashed map will usually only satisfy checks 1 and 3, since its art is not
built one-mesh-per-proxy. That is expected; check 2 reports what it could pair
up.
"""

import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from enemy_types import enemy_types
import prune_tree_proxies

COMPONENT = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2),
             5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}
COUNT = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}

# Tolerance in metres. The exporter rounds to 5 decimals and glTF stores floats,
# so agreement is exact to well within a millimetre when it is right; anything
# above this is a real disagreement, not accumulated error.
TOLERANCE = 0.002

# The detail mesh is checked against the level's declared bounds rather than
# against the exporter's own arithmetic, so it gets a looser margin: a blade
# leaning off the terrain's outer edge is fine, a chunk of meadow floating a
# hundred metres away is not.
BOUNDS_TOLERANCE = 1.0

# Blender writes u16 indices only while the largest index is under 65535
# (io_scene_gltf2/blender/exp/primitives.py:218), and raylib cannot read
# anything wider -- Mesh.indices is `unsigned short *` and the draw call
# hardcodes GL_UNSIGNED_SHORT.
MAX_VERTS_PER_PRIMITIVE = 65534
COMPONENT_USHORT = 5123


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


# --- Collision mesh ---------------------------------------------------------

# How far the mesh surface may sit from a spawn marker's own Y. The markers are
# placed by raycasting the *source* terrain in Blender, while the mesh is that
# terrain decimated, so a small disagreement is the decimation and not a bug.
SPAWN_TOLERANCE = 0.35


def load_collision_bin(path):
    """Parse the flat soup written by export_level.py's export_collision_mesh."""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 16 or data[:4] != b"SKCM":
        raise SystemExit("%s is not a collision mesh (bad magic)" % path)
    version, vertex_count, triangle_count = struct.unpack("<III", data[4:16])
    if version != 1:
        raise SystemExit("%s is version %d, this script reads 1"
                         % (path, version))
    offset = 16
    floats = vertex_count * 3
    verts = struct.unpack("<%df" % floats, data[offset:offset + floats * 4])
    offset += floats * 4
    ints = triangle_count * 3
    idx = struct.unpack("<%dI" % ints, data[offset:offset + ints * 4])
    return [tuple(verts[i * 3:i * 3 + 3]) for i in range(vertex_count)], idx


def triangle_normal(a, b, c):
    e1 = [b[i] - a[i] for i in range(3)]
    e2 = [c[i] - a[i] for i in range(3)]
    n = [e1[1] * e2[2] - e1[2] * e2[1],
         e1[2] * e2[0] - e1[0] * e2[2],
         e1[0] * e2[1] - e1[1] * e2[0]]
    length = math.sqrt(sum(v * v for v in n))
    return None if length < 1e-9 else [v / length for v in n]


def ground_under(verts, idx, x, z, from_y=1000.0):
    """Nearest surface below (x, from_y, z): (y, normal). Brute force."""
    best = None
    for t in range(len(idx) // 3):
        a, b, c = (verts[idx[t * 3]], verts[idx[t * 3 + 1]],
                   verts[idx[t * 3 + 2]])
        # Moller-Trumbore specialised to a straight-down ray.
        e1 = [b[i] - a[i] for i in range(3)]
        e2 = [c[i] - a[i] for i in range(3)]
        h = [-e2[2], 0.0, e2[0]]
        det = sum(e1[i] * h[i] for i in range(3))
        if abs(det) < 1e-12:
            continue
        inv = 1.0 / det
        s = [x - a[0], from_y - a[1], z - a[2]]
        u = inv * sum(s[i] * h[i] for i in range(3))
        if u < -1e-6 or u > 1.0 + 1e-6:
            continue
        q = [s[1] * e1[2] - s[2] * e1[1],
             s[2] * e1[0] - s[0] * e1[2],
             s[0] * e1[1] - s[1] * e1[0]]
        v = inv * -q[1]
        if v < -1e-6 or u + v > 1.0 + 1e-6:
            continue
        dist = inv * sum(e2[i] * q[i] for i in range(3))
        if dist > 1e-6 and (best is None or dist < best[0]):
            best = (dist, triangle_normal(a, b, c))
    if best is None:
        return None
    return from_y - best[0], best[1]


def check_tree_proxies(level, failures):
    """Report tree collision boxes nothing can reach or see past.

    A note rather than a failure. Unreachable proxies are wasted work, not
    wrong -- the level plays identically with them. What makes this worth
    checking is that tools/export_level.py rebuilds `obstacles` from the .blend
    every time, so a level that was pruned and then re-exported silently gets
    them all back. This is where that shows up, in the check that is already
    run after an export.
    """
    try:
        reach, trees, touch, sight, removable = prune_tree_proxies.classify(level)
    except prune_tree_proxies.PruneError as error:
        print("  (not checked: %s)" % error)
        return
    if not trees:
        print("  (no BOX_Tree_* proxies)")
        return

    if removable:
        print("  note  %d of %d tree proxies are unreachable and block no "
              "sightline." % (len(removable), len(trees)))
        print("        They cost a per-character obstacle test every frame and "
              "%d triangles in every navmesh bake, for geometry nothing can "
              "touch. Run:" % (len(removable) * 12))
        print("          python3 tools/prune_tree_proxies.py <level-dir>")
    else:
        print("  OK    %d tree proxies, all reachable or sight-blocking"
              % len(trees))


def check_enemy_spawns(level, level_dir, declared, overlay, spawns_effective,
                       failures):
    """Validate the hand-authored spawn overlay.

    Runs regardless of format, because the overlay is the one file in the
    pipeline a person types and it is therefore the one most likely to be
    wrong -- while `collision.bin`, which the ground test needs, is only
    present on format 2. So the checks are layered: schema and bounds work on
    every level, and the ground test adds itself when there is a mesh to test
    against.

    The height printout for entries that omitted `y` is the real product here.
    That number is what the engine will snap the spawn to, and it is otherwise
    something the author has to guess.
    """
    if overlay is None:
        print("  (no %s — this level's enemies come from its Blender markers)"
              % OVERLAY_NAME)
        return

    if not isinstance(overlay, dict):
        failures.append("enemies.json")
        print("  FAIL  %s is not a JSON object" % OVERLAY_NAME)
        return
    if overlay.get("format") != 1:
        failures.append("enemies.json format")
        print("  FAIL  %s is format %r; this build reads format 1"
              % (OVERLAY_NAME, overlay.get("format")))
        return
    if not isinstance(overlay.get("spawns"), list):
        failures.append("enemies.json spawns")
        print("  FAIL  %s has no \"spawns\" array" % OVERLAY_NAME)
        return

    try:
        known = set(enemy_types())
    except SystemExit:
        known = None

    allowed = {"type", "x", "y", "z", "yaw", "maxHealth", "vision",
               "startAwareness"}
    bad = 0

    for i, spawn in enumerate(overlay["spawns"]):
        label = "spawn %d" % i
        if not isinstance(spawn, dict):
            bad += 1
            print("  FAIL  %-12s is not an object" % label)
            continue

        unknown = sorted(set(spawn) - allowed)
        if unknown:
            print("  note  %-12s has unrecognised key(s) %s — the loader will "
                  "ignore them" % (label, ", ".join(unknown)))

        kind = spawn.get("type")
        if known is not None and kind not in known:
            bad += 1
            print("  FAIL  %-12s type %r is not in EnemyTypes.def (known: %s)"
                  % (label, kind, ", ".join(sorted(known))))
        for key in ("x", "z"):
            if not isinstance(spawn.get(key), (int, float)):
                bad += 1
                print("  FAIL  %-12s has no numeric %r" % (label, key))
        if isinstance(spawn.get("maxHealth"), (int, float)) \
                and spawn["maxHealth"] <= 0:
            bad += 1
            print("  FAIL  %-12s maxHealth %.1f would spawn something already "
                  "dead" % (label, spawn["maxHealth"]))
        awareness = spawn.get("startAwareness")
        if isinstance(awareness, (int, float)) and not 0 <= awareness <= 200:
            print("  note  %-12s startAwareness %.0f is outside the 0-200 "
                  "scale (100 suspicious, 200 aware)" % (label, awareness))

    if bad:
        failures.append("enemies.json entries")

    # Inside the level, in plan. The one geometric check that works on every
    # level, and it catches the commonest paste error by a distance: an x/z
    # carried over from a different map, or the two swapped.
    outside = 0
    for label, _kind, x, _y, z in spawns_effective:
        if not isinstance(x, (int, float)) or not isinstance(z, (int, float)):
            continue
        if not (declared[0][0] - BOUNDS_TOLERANCE <= x
                <= declared[1][0] + BOUNDS_TOLERANCE
                and declared[0][2] - BOUNDS_TOLERANCE <= z
                <= declared[1][2] + BOUNDS_TOLERANCE):
            outside += 1
            failures.append(label)
            print("  FAIL  %-12s at x=%.1f z=%.1f is outside the level bounds"
                  % (label, x, z))
    if not outside:
        print("  OK    %d spawn(s), all inside the level bounds"
              % len(spawns_effective))

    # The ground pass. Explicit heights were already tested by check 3; what is
    # printed here is the height the engine will pick for the ones that left it
    # out.
    name = level.get("collisionMesh")
    if not name or not os.path.exists(os.path.join(level_dir, name)):
        print("  note  no collision mesh — heights for spawns without an "
              "explicit \"y\" cannot be resolved here. The engine snaps them "
              "against the BOX_/RAMP_ proxies at load; that path is not "
              "reimplemented in this script on purpose, because a second "
              "implementation of containsXZ and getHeightAt is exactly the "
              "drift this file exists to catch.")
        return

    verts, idx = load_collision_bin(os.path.join(level_dir, name))
    for label, _kind, x, y, z in spawns_effective:
        if y is not None:
            continue
        if not isinstance(x, (int, float)) or not isinstance(z, (int, float)):
            continue
        found = ground_under(verts, idx, x, z)
        if found is None:
            failures.append(label)
            print("  FAIL  %-12s no mesh under it — the engine will drop this "
                  "spawn" % label)
            continue
        resolved, normal = found
        if normal is None or normal[1] < 0.4:
            failures.append(label)
            print("  FAIL  %-12s lands on a surface facing %+.2f, which the "
                  "engine reads as a wall"
                  % (label, normal[1] if normal else 0.0))
        else:
            print("  OK    %-12s will snap to y=%.2f (normal.y %+.2f)"
                  % (label, resolved, normal[1]))


def check_detail_mesh(level, level_dir, declared, failures):
    """Verify the detail mesh is something raylib will actually draw.

    Every failure here is one that produces no error at runtime, which is why
    it is worth a check rather than a comment.

    A primitive over 65,534 vertices makes Blender emit u32 indices
    (io_scene_gltf2/blender/exp/primitives.py:218), and raylib's Mesh.indices is
    `unsigned short *` -- LoadGLTF narrows the accessor mod 65536 with a
    LOG_WARNING and the level arrives with its triangles wired to the wrong
    vertices. The index type is checked as well as the count, because they are
    the same fault seen from two directions and neither is visible in Blender.

    A missing COLOR_0 is the export_vertex_color trap: under Blender's default
    'MATERIAL' the attribute is dropped unless the material's node tree
    references it, and export_all_vertex_colors then substitutes one filled with
    literal 255. The grass would ship flat -- no contact shading, no per-blade
    variation -- and look merely disappointing rather than broken.

    And the AABB is checked against the declared bounds because the exporter
    deliberately leaves detail geometry out of the bounds union: that is only
    sound while the detail actually stays inside them.
    """
    name = level.get("detailModel")
    if not name:
        print("  (no detailModel — this level ships no drawn-only scenery)")
        return

    path = os.path.join(level_dir, name)
    if not os.path.exists(path):
        failures.append("detail mesh")
        print("  FAIL  %s is named in level.json but not on disk" % name)
        return

    gltf, buffer = load_glb(path)
    accessors = gltf["accessors"]
    widest, triangles, primitives = 0, 0, 0
    bad_index, no_colour = [], []

    for mesh in gltf.get("meshes", []):
        for prim in mesh.get("primitives", []):
            primitives += 1
            label = mesh.get("name", "?")
            position = prim.get("attributes", {}).get("POSITION")
            if position is None:
                continue
            widest = max(widest, accessors[position]["count"])
            if accessors[position]["count"] > MAX_VERTS_PER_PRIMITIVE:
                bad_index.append("%s (%d verts)"
                                 % (label, accessors[position]["count"]))
            index = prim.get("indices")
            if index is not None:
                triangles += accessors[index]["count"] // 3
                if accessors[index]["componentType"] != COMPONENT_USHORT:
                    bad_index.append("%s (u32 indices)" % label)
            if "COLOR_0" not in prim.get("attributes", {}):
                no_colour.append(label)

    if bad_index:
        failures.append("detail indices")
        print("  FAIL  %d primitive(s) raylib will truncate to 16-bit "
              "indices: %s" % (len(bad_index), ", ".join(bad_index[:4])))
    else:
        print("  OK    indices: %d primitives, widest %d verts (%.0f%% of the "
              "%d cap), all u16"
              % (primitives, widest, 100.0 * widest / MAX_VERTS_PER_PRIMITIVE,
                 MAX_VERTS_PER_PRIMITIVE))

    if no_colour:
        failures.append("detail vertex colours")
        print("  FAIL  %d primitive(s) carry no COLOR_0: %s\n"
              "        export_level.py must pass vertex_colors=True, or "
              "Blender drops the attribute." % (len(no_colour),
                                                ", ".join(no_colour[:4])))
    else:
        print("  OK    every primitive carries COLOR_0")

    _, overall = mesh_bounds(gltf, buffer)
    if overall is None:
        failures.append("detail geometry")
        print("  FAIL  %s contains no mesh geometry" % name)
        return
    outside = [i for i in range(3)
               if overall[0][i] < declared[0][i] - BOUNDS_TOLERANCE
               or overall[1][i] > declared[1][i] + BOUNDS_TOLERANCE]
    if outside:
        failures.append("detail bounds")
        print("  FAIL  detail reaches outside the declared bounds on %s: %s\n"
              "        the exporter leaves detail out of the bounds union, "
              "which only holds while it stays inside them"
              % ("/".join("xyz"[i] for i in outside), format_box(overall)))
    else:
        print("  OK    inside the declared bounds, %d triangles" % triangles)


OVERLAY_NAME = "enemies.json"


def load_enemy_overlay(level_dir):
    """The level's enemies.json, or None when it has none.

    Returns the parsed object without judging it -- check 5 does that. Read
    here as well because check 3 has to know which spawns the *game* will
    actually use, which stopped being level.json's list the moment overlays
    existed.
    """
    path = os.path.join(level_dir, OVERLAY_NAME)
    if not os.path.exists(path):
        return None
    with open(path) as f:
        return json.load(f)


def effective_spawns(level, overlay):
    """(label, type, x, y_or_None, z) for every enemy the game will build.

    This is the list check 3 must ground-test. Testing level.json's spawns once
    an overlay exists would check positions the game does not use -- and pass,
    while the real ones hang in the air.
    """
    if overlay is not None and isinstance(overlay.get("spawns"), list):
        out = []
        for i, spawn in enumerate(overlay["spawns"]):
            if not isinstance(spawn, dict):
                continue
            out.append(("%s_%02d" % (spawn.get("type", "?"), i),
                        spawn.get("type"), spawn.get("x"),
                        spawn.get("y"), spawn.get("z")))
        return out

    out = []
    for i, spawn in enumerate(level.get("enemySpawns", [])):
        p = spawn["position"]
        out.append(("%s_%02d" % (spawn["type"], i), spawn["type"],
                    p[0], p[1], p[2]))
    return out


def check_collision_mesh(level, level_dir, spawns_effective, failures):
    """Verify the mesh is where the level says it is, and the right way up.

    The winding check is the important one and it is cheap. `to_game` is a
    rotation with determinant +1, so it preserves handedness -- but that is
    unobvious enough that "compensating" for it by swapping two corners is an
    easy mistake, and it inverts every normal in the level. PhysicsManager reads
    the sign of normal.y to tell a floor from a ceiling
    (classifySurfaceNormal), so an inverted soup is a world with no floors at
    all, and nothing else in this script would notice.
    """
    name = level.get("collisionMesh")
    if not name:
        print("  (level ships no collision mesh — proxies only)")
        return

    path = os.path.join(level_dir, name)
    if not os.path.exists(path):
        failures.append("collision mesh")
        print("  FAIL  %s is declared but missing" % name)
        return

    verts, idx = load_collision_bin(path)
    triangles = len(idx) // 3
    print("  %s: %d vertices, %d triangles" % (name, len(verts), triangles))

    up = down = degenerate = 0
    for t in range(triangles):
        n = triangle_normal(verts[idx[t * 3]], verts[idx[t * 3 + 1]],
                            verts[idx[t * 3 + 2]])
        if n is None:
            degenerate += 1
        elif n[1] > 0.5:
            up += 1
        elif n[1] < -0.5:
            down += 1

    if up > down:
        print("  OK    winding: %d up-facing vs %d down-facing" % (up, down))
    else:
        failures.append("winding")
        print("  FAIL  winding: %d up-facing vs %d down-facing -- the soup "
              "looks inside out.\n"
              "          to_game has determinant +1 and preserves handedness; "
              "reversing\n"
              "          triangle order to 'compensate' turns every floor into "
              "a ceiling." % (up, down))
    if degenerate:
        print("  note  %d degenerate triangles (%.1f%%); the engine drops these"
              % (degenerate, 100.0 * degenerate / triangles))

    # The player spawn always comes from level.json; the enemies come from
    # whichever source the game will actually read. An entry that omitted its
    # `y` has nothing to compare against, so it is reported by check 5 with its
    # resolved height instead of being tested here.
    spawns = [("PLAYER_SPAWN", level["playerSpawn"]["position"])]
    for label, _type, x, y, z in spawns_effective:
        if y is None:
            continue
        spawns.append((label, [x, y, z]))

    for label, position in spawns:
        found = ground_under(verts, idx, position[0], position[2])
        if found is None:
            failures.append(label)
            print("  FAIL  %-16s no mesh surface underneath it" % label)
            continue
        y, normal = found
        delta = abs(y - position[1])
        if delta > SPAWN_TOLERANCE:
            failures.append(label)
            print("  FAIL  %-16s stands at y=%.2f but the mesh is at %.2f"
                  % (label, position[1], y))
        elif normal is None or normal[1] < 0.4:
            failures.append(label)
            print("  FAIL  %-16s stands on a surface facing %+.2f, which "
                  "PhysicsManager reads as a wall or ceiling"
                  % (label, normal[1] if normal else 0.0))
        else:
            print("  OK    %-16s mesh %.2f m away, normal.y %+.2f"
                  % (label, delta, normal[1]))


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

    try:
        overlay = load_enemy_overlay(level_dir)
    except (OSError, ValueError) as error:
        overlay = None
        print("\n  (enemies.json present but unreadable: %s)" % error)
    spawns_effective = effective_spawns(level, overlay)

    print("\n3. Collision mesh")
    check_collision_mesh(level, level_dir, spawns_effective, failures)

    print("\n4. Detail mesh")
    check_detail_mesh(level, level_dir, declared, failures)

    print("\n5. Enemy spawns")
    check_enemy_spawns(level, level_dir, declared, overlay, spawns_effective,
                       failures)

    print("\n6. Tree proxies")
    check_tree_proxies(level, failures)

    print()
    if failures:
        # A count, not a fraction. The denominator used to be `paired + 1`,
        # which stopped meaning anything once checks 4 and 5 could fail too --
        # it read "3 of 1" on a level with one paired proxy and three faults.
        print("FAILED: %d problem(s): %s.\n"
              "For a misplaced proxy: a uniform offset means the axis\n"
              "conversion is wrong; a rotation that only shows on yawed\n"
              "proxies means the yaw sign is."
              % (len(failures), ", ".join(sorted(set(failures)))))
        return 1

    print("PASSED: collision proxies and visual mesh agree to within %.0fmm."
          % (TOLERANCE * 1000))
    return 0


if __name__ == "__main__":
    sys.exit(main())
