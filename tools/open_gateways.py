"""Raise the portcullis grilles in placed gatehouses until they can be walked under.

The castle's `wall entrance` kit piece carries a portcullis: a grid of barred
metal filling its archway, 2,400 of the piece's 4,517 faces. It is not only a
visual barrier -- buildings go into COLLISION_MESH, so the bars are solid.

Measured on this art, the grille hung from the arch soffit down to z 0.56 with
the ground at 0.14: a 0.42 gap, 1.26 m at WORLD_SCALE, against a player 1.8 m
tall. The gateway was impassable. The archway itself is not the problem -- its
soffit is at 1.01, giving 2.6 m of headroom -- so the fix is to lift the grille
rather than remove it. The bars retract up into the gatehouse the way a real
portcullis does, leaving their spiked tips showing under the arch.

Raising alone is not enough, and the first attempt at it looked broken. The
grille was authored to fill the archway exactly, so lifting it pushes its top
rail and guide bars out through the wall face above the arch, standing proud of
the stone in mid-air. So the grille is lifted *and then cut off* at the height
its top used to sit at -- which is the arch line, because that is what it was
built to meet. Everything above that is discarded, the cut is concealed by the
arch stone, and what remains hanging under the arch is the spiked bottom of the
portcullis at its authored proportions.

Metal only *finds* the grille; it does not define it. The portcullis is built
as individual bars, each its own connected component, and four of the eleven
carry a stone material rather than a metal one. A material-only selection
lifts seven bars and leaves four hanging at their original height, still
blocking the doorway at head height -- which is exactly what the first pass
shipped.

Selecting by face plane instead is no good either: it clips faces off the arch
component, whose vertices are then shared with geometry that is not moving, and
the bars tear rather than travel.

So the unit of selection is the **connected component**. The bars are separate
shells 0.03 thick; the arch and jambs are one shell 0.49 thick spanning the
wall. A component is part of the portcullis when it is thin and sits in the
plane the metal occupies, whatever material it wears. Components are disjoint
by construction, so nothing is shared and nothing tears.

Shape is still what confirms a grille exists at all: act only where the metal
forms a slab thinner than GRATE_MAX_THICKNESS. `castle.001` and
`castle tower.001` carry metal too, as finials at z 13-15 on the roofline, and
acting on those would levitate the keep's spires. Measured here the two cases
are far apart -- the grille is 0.03 thick, the thinnest finial cluster 0.32 --
so the threshold is not finely balanced.

Only vertices used *exclusively* by grille faces move, or the surrounding stone
would be dragged up with them. Only placed structures are touched; the parked
kit originals keep their portcullis where it was.

Usage:
    blender --background <level>.blend --python tools/open_gateways.py -- \
        [--out <path>] [--dry-run]
"""

import argparse
import sys

import bpy
import bmesh
from mathutils import Vector

STRUCTURE_COLLECTIONS = ("finals buildings", "buildings")
PARKED_MAX_Z = -10.0
TERRAIN = ("ground", "ground_east")

GRATE_MATERIALS = ("metal", "metal.001")

# A grille is a panel. Anything whose metal is thicker than this on every axis
# is a solid ornament and is left alone.
GRATE_MAX_THICKNESS = 0.15

# How far off the grille's own plane a face may sit and still count as part of
# it, and how far past its span on the other two axes.
GRATE_PLANE_TOLERANCE = 0.05
GRATE_SPAN_MARGIN = 0.10

# Headroom to leave under the raised grille, in authored units. The player is
# 1.8 m and WORLD_SCALE is 3, so 0.6 is exactly head height; 0.75 is 2.25 m,
# a quarter again for the camera and for uneven ground under the arch.
PASSAGE_CLEARANCE = 0.75


def terrain_height(x, y):
    best = None
    for name in TERRAIN:
        obj = bpy.data.objects.get(name)
        if obj is None:
            continue
        inv = obj.matrix_world.inverted()
        hit, loc, _, _ = obj.ray_cast(
            inv @ Vector((x, y, 1000.0)),
            (inv.to_3x3() @ Vector((0.0, 0.0, -1.0))).normalized())
        if hit:
            z = (obj.matrix_world @ loc).z
            if best is None or z > best:
                best = z
    return best


def placed_structures():
    out = []
    for name in STRUCTURE_COLLECTIONS:
        collection = bpy.data.collections.get(name)
        if collection is None:
            continue
        for obj in collection.objects:
            if obj.type != "MESH":
                continue
            top = max((obj.matrix_world @ Vector(c)).z for c in obj.bound_box)
            if top > PARKED_MAX_Z:
                out.append(obj)
    return sorted(out, key=lambda o: o.name)


def grate(obj):
    """The portcullis, as whole connected components in the grille's plane."""
    materials = [m.name if m else None for m in obj.data.materials]
    metal = {i for i, m in enumerate(materials) if m in GRATE_MATERIALS}
    if not metal:
        return None

    matrix = obj.matrix_world
    seed = [p for p in obj.data.polygons if p.material_index in metal]
    if not seed:
        return None
    pts = [matrix @ obj.data.vertices[v].co for p in seed for v in p.vertices]
    lo = [min(q[a] for q in pts) for a in range(3)]
    hi = [max(q[a] for q in pts) for a in range(3)]
    extent = [hi[a] - lo[a] for a in range(3)]
    thin = min(range(3), key=lambda a: extent[a])
    if extent[thin] > GRATE_MAX_THICKNESS:
        return {"thickness": extent[thin], "faces": None}

    mid = (lo[thin] + hi[thin]) * 0.5
    other = [a for a in range(3) if a != thin]

    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bm.faces.ensure_lookup_table()

    seen, faces = set(), set()
    for face in bm.faces:
        if face.index in seen:
            continue
        stack, component = [face], []
        seen.add(face.index)
        while stack:
            current = stack.pop()
            component.append(current)
            for edge in current.edges:
                for other_face in edge.link_faces:
                    if other_face.index not in seen:
                        seen.add(other_face.index)
                        stack.append(other_face)

        corners = [matrix @ v.co for f in component for v in f.verts]
        c_lo = [min(q[a] for q in corners) for a in range(3)]
        c_hi = [max(q[a] for q in corners) for a in range(3)]
        # A bar: thin, in the grille's plane, and inside its span.
        if c_hi[thin] - c_lo[thin] > GRATE_MAX_THICKNESS:
            continue
        if abs((c_lo[thin] + c_hi[thin]) * 0.5 - mid) > GRATE_PLANE_TOLERANCE:
            continue
        if any(c_lo[a] < lo[a] - GRATE_SPAN_MARGIN
               or c_hi[a] > hi[a] + GRATE_SPAN_MARGIN for a in other):
            continue
        faces.update(f.index for f in component)
    bm.free()

    if not faces:
        return None

    gverts, overts = set(), set()
    for poly in obj.data.polygons:
        (gverts if poly.index in faces else overts).update(poly.vertices)
    pts = [matrix @ obj.data.vertices[v].co for v in gverts]
    by_mat = {}
    for i in faces:
        nm = materials[obj.data.polygons[i].material_index]
        by_mat[nm] = by_mat.get(nm, 0) + 1
    return {
        "faces": faces,
        "exclusive": gverts - overts,
        "shared": len(gverts & overts),
        "thickness": extent[thin],
        "by_material": by_mat,
        "z": (min(q.z for q in pts), max(q.z for q in pts)),
        "xy": ((min(q.x for q in pts) + max(q.x for q in pts)) * 0.5,
               (min(q.y for q in pts) + max(q.y for q in pts)) * 0.5),
    }


def parse_args(argv):
    argv = argv[argv.index("--") + 1:] if "--" in argv else []
    parser = argparse.ArgumentParser(prog="open_gateways.py")
    parser.add_argument("--out", default=None)
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args(argv)


def main():
    args = parse_args(sys.argv)

    for name in ("landscape", "buildings", "rocks", "trees",
                 "finals buildings", "final rocks"):
        collection = bpy.data.collections.get(name)
        if collection is not None:
            collection.hide_viewport = False
    bpy.context.view_layer.update()

    raised = 0
    for obj in placed_structures():
        info = grate(obj)
        if info is None:
            continue
        if info.get("faces") is None:
            print("[open_gateways] kept   %-20s metal %.2f thick (ornament, "
                  "not a grille)" % (obj.name, info["thickness"]))
            continue

        ground = terrain_height(*info["xy"])
        if ground is None:
            print("[open_gateways] SKIPPED %-20s no terrain under the grille"
                  % obj.name)
            continue

        lift = (ground + PASSAGE_CLEARANCE) - info["z"][0]
        if lift <= 0.0:
            print("[open_gateways] kept   %-20s already clears %.2f "
                  "(gap %.2f)" % (obj.name, PASSAGE_CLEARANCE,
                                  info["z"][0] - ground))
            continue

        if args.dry_run:
            print("[open_gateways] would raise %-20s by %.2f "
                  "(grille z %.2f-%.2f, ground %.2f, %d shared verts pinned)"
                  % (obj.name, lift, info["z"][0], info["z"][1], ground,
                     info["shared"]))
            raised += 1
            continue

        euler = obj.matrix_world.to_euler()
        scale = obj.matrix_world.to_scale()
        if max(abs(a) for a in euler) > 1e-6:
            # Rotation tilts the cut plane out of local Z and there is no
            # scalar that recovers it. Scale is fine; rotation is not.
            print("[open_gateways] SKIPPED %-20s is rotated; the cut plane "
                  "would be tilted" % obj.name)
            continue
        if abs(scale.z) < 1e-9:
            print("[open_gateways] SKIPPED %-20s has zero Z scale" % obj.name)
            continue

        # Local Z maps to world Z as world = location.z + scale.z * local, so
        # both the cut plane and the lift are divided through by the scale.
        # Skipping a scaled gatehouse instead -- as this did at first -- leaves
        # the portcullis at its authored height and the gateway impassable,
        # which is a worse failure than the one the guard was protecting from.
        cut_world = info["z"][1]          # the arch line the grille was built to
        cut_local = (cut_world - obj.location.z) / scale.z

        bm = bmesh.new()
        bm.from_mesh(obj.data)
        bm.verts.ensure_lookup_table()
        bm.faces.ensure_lookup_table()

        gfaces = [bm.faces[i] for i in info["faces"] if i < len(bm.faces)]
        gverts = {v for f in gfaces for v in f.verts}
        overts = {v for f in bm.faces if f.index not in info["faces"]
                  for v in f.verts}
        lift_local = lift / scale.z
        for vert in gverts - overts:
            vert.co.z += lift_local

        gedges = {e for f in gfaces for e in f.edges}
        bmesh.ops.bisect_plane(
            bm, geom=list(gverts) + list(gedges) + gfaces,
            plane_co=(0.0, 0.0, cut_local), plane_no=(0.0, 0.0, 1.0),
            clear_outer=True)
        loose = [v for v in bm.verts if not v.link_faces]
        if loose:
            bmesh.ops.delete(bm, geom=loose, context="VERTS")
        bm.to_mesh(obj.data)
        bm.free()
        obj.data.update()

        raised += 1
        print("[open_gateways] raised %-20s by %.2f and cut at the arch line "
              "%.2f -> spikes hang %.2f-%.2f, clearance %.2f (%.2f m shipped)"
              % (obj.name, lift, cut_world, info["z"][0] + lift, cut_world,
                 PASSAGE_CLEARANCE, PASSAGE_CLEARANCE * 3.0))
        print("[open_gateways]        grille was %s"
              % ", ".join("%d %s" % (n, m)
                          for m, n in sorted(info["by_material"].items(),
                                             key=lambda kv: -kv[1])))

    print("[open_gateways] %d gateway(s) made passable" % raised)

    if not args.dry_run and raised:
        out = args.out or bpy.data.filepath
        bpy.ops.wm.save_as_mainfile(filepath=out, compress=False)
        print("[open_gateways] wrote %s" % out)


if __name__ == "__main__":
    main()
