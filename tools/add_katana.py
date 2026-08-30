"""Import the low-poly katana and rigid-bind it into the model's right fist.

Runs against pack_sekiro.blend -- the RETARGETED file, where every mesh is
already on the 69-bone Mixamo rig. Run it against the LIVE session, before
tools/merge_animations.py:

    exec(open("tools/add_katana.py").read())
    report = main()

The katana is bound the same way ` Sword.002` is: one vertex group, one bone,
full weight, plus an armature modifier. One bone at full weight is what makes it
STATIC -- it cannot bend or stretch, it only follows the joint.

Nothing here is hardcoded to the katana's local coordinates, and that is
deliberate. Joining the FBX's five sword shells leaves the origin on whichever
shell Blender used as the join target, so the same mesh can come out of two
imports with its grip at local y=+0.575 or at y=-0.0535. Every offset below is
measured off the geometry instead:

  * The long axis is whichever local axis has the largest span.
  * The TSUBA is the widest cross-section along it -- the guard is a disc, so it
    spikes the radial profile well clear of both blade and grip.
  * The GRIP is the short side of the tsuba (0.27 m) and the blade the long one
    (0.87 m), which is what fixes the sign.
  * The cutting EDGE is the convex side of the blade's curve, from how far the
    blade centreline bows off the chord between its ends.

The hand frame is measured too. The grip of a closed fist runs through the tube
the fingers make, perpendicular to both the palm plane and the finger direction,
so it is the cross product of the palm normal and the hand bone. The palm normal
is the minimum-variance axis of the palm shell -- the only axis of that
10-vertex shell that is not degenerate. Toe bones give the facing, which picks
the sign so the blade points forward rather than backward out of the fist.
"""

import os

import bpy
from mathutils import Matrix, Vector

FBX = "/Users/long/Documents/3D/Model/low-poly-katana/source/Low-Poly Katana.fbx"
TEX = "/Users/long/Documents/3D/Model/low-poly-katana/textures"

MAIN_ARM = "Armature"
HAND_BONE = "mixamorig:RightHand"
TOE_BONE = "mixamorig:RightToeBase"
HAND_MESH = "Hand"
KATANA = "Katana"
SAYA = "Katana_Saya"

# How far behind the guard the fist closes. A katana's front hand sits just
# under the tsuba; the grip is 0.27 m, so this leaves the rest trailing back
# past the wrist the way a real one-handed hold does.
TSUBA_TO_FIST = 0.085

# Bins used to profile the blade. 40 over 1.14 m puts the guard, which is about
# 30 mm deep, alone in its own bin.
PROFILE_BINS = 40


def _activate(obj, *others):
    for o in bpy.data.objects:
        o.select_set(False)
    for o in (obj,) + others:
        o.select_set(True)
    bpy.context.view_layer.objects.active = obj


def _join(parts, name):
    lead = max(parts, key=lambda o: len(o.data.vertices))
    _activate(lead, *[p for p in parts if p is not lead])
    bpy.ops.object.join()
    lead.name = name
    lead.data.name = name
    _activate(lead)
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    return lead


def import_katana():
    """Import the FBX and fold its ten shells into a katana and a scabbard."""
    if bpy.data.objects.get(KATANA):
        return bpy.data.objects[KATANA], bpy.data.objects.get(SAYA), "already present"
    before = set(o.name for o in bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=FBX)
    new = [bpy.data.objects[n] for n in set(o.name for o in bpy.data.objects) - before]
    blade = [o for o in new if o.type == "MESH" and "Saya" not in o.name]
    saya = [o for o in new if o.type == "MESH" and "Saya" in o.name]
    if not blade:
        raise RuntimeError("FBX produced no katana meshes")
    return _join(blade, KATANA), (_join(saya, SAYA) if saya else None), "imported"


def fix_material(obj):
    """Repoint the maps at the real files and drop the stray colour attribute.

    The FBX carries its four maps embedded. The importer unpacks them beside the
    FBX into a `Low-Poly Katana.fbm/` folder and points the images there; that
    folder is temporary, so the paths dangle and the pixels survive only as long
    as the session. The same maps ship as `.jpeg` next to the FBX.

    The colour attribute has to go for a different reason: nothing reads it, but
    it still exports as COLOR_0, and the skinning shader multiplies COLOR_0 into
    finalColor -- so an unused one renders the katana black in game. See
    assets/shaders/glsl330/skinning.fs.
    """
    fixed = []
    for mat in obj.data.materials:
        if not mat or not mat.use_nodes:
            continue
        for node in mat.node_tree.nodes:
            if node.type != "TEX_IMAGE" or not node.image:
                continue
            base = os.path.splitext(os.path.basename(node.image.filepath))[0]
            cand = os.path.join(TEX, base + ".jpeg")
            if os.path.exists(cand):
                node.image.filepath = cand
                node.image.reload()
                fixed.append(base)
    dropped = [a.name for a in obj.data.color_attributes]
    for a in list(obj.data.color_attributes):
        obj.data.color_attributes.remove(a)
    return {"textures": fixed, "colour_attrs_dropped": dropped}


def katana_axes(k):
    """Guard position, which side the grip is on, and which side the edge is on."""
    co = [v.co.copy() for v in k.data.vertices]
    span = [max(p[i] for p in co) - min(p[i] for p in co) for i in range(3)]
    axis = span.index(max(span))
    if axis != 1:
        raise RuntimeError("expected the long axis on local Y, got %s" % "XYZ"[axis])
    lo, hi = min(p.y for p in co), max(p.y for p in co)

    # The guard is the widest cross-section: a disc on an otherwise slim shaft.
    best = None
    for i in range(PROFILE_BINS):
        a = lo + (hi - lo) * i / PROFILE_BINS
        b = lo + (hi - lo) * (i + 1) / PROFILE_BINS
        sl = [p for p in co if a <= p.y <= b]
        if not sl:
            continue
        cx = sum(p.x for p in sl) / len(sl)
        cz = sum(p.z for p in sl) / len(sl)
        r = max(((p.x - cx) ** 2 + (p.z - cz) ** 2) ** 0.5 for p in sl)
        if best is None or r > best[1]:
            best = ((a + b) / 2, r)
    tsuba_y = best[0]

    # Grip is the short side of the guard, blade the long one.
    sign = 1.0 if (hi - tsuba_y) < (tsuba_y - lo) else -1.0

    # The blade bows off the chord between its ends; the edge is the convex side.
    bins = {}
    for p in co:
        if (p.y - tsuba_y) * sign < 0:
            bins.setdefault(round(p.y, 2), []).append(p.z)
    line = sorted((y, sum(z) / len(z)) for y, z in bins.items() if len(z) >= 4)
    (y0, z0), (y1, z1) = line[0], line[-1]
    bulge = sum(z - (z0 + (z1 - z0) * (y - y0) / (y1 - y0)) for y, z in line)

    return {"tsuba_y": tsuba_y, "sign": sign, "bulge": bulge,
            "convex_z": 1.0 if bulge > 0 else -1.0,
            "hold_y": tsuba_y + sign * TSUBA_TO_FIST,
            "grip_end_y": hi if sign > 0 else lo,
            "tip_y": lo if sign > 0 else hi}


def hand_frame(main):
    """Palm centre, palm normal and the axis a closed fist grips along."""
    M = main.matrix_world
    hand = bpy.data.objects[HAND_MESH]
    gi = {vg.name: vg.index for vg in hand.vertex_groups}
    palm_i = gi[HAND_BONE]
    digits = {gi[n] for n in gi
              if any(s in n for s in ("Thumb", "Index", "Middle", "Ring", "Pinky"))}

    pts = []
    for v in hand.data.vertices:
        w = {g.group: g.weight for g in v.groups}
        if w.get(palm_i, 0.0) > 0.5 and max([w[i] for i in digits if i in w] or [0]) < 0.3:
            pts.append(hand.matrix_world @ v.co)
    if len(pts) < 6:
        raise RuntimeError("only %d palm vertices found" % len(pts))
    centre = sum(pts, Vector()) / len(pts)

    # Smallest eigenvector of the covariance = the palm's thickness axis. Power
    # iteration on (trace*I - C) converges to it; the other two axes of this
    # shell are near-degenerate and cannot be told apart.
    C = [[sum((p - centre)[i] * (p - centre)[j] for p in pts) for j in range(3)]
         for i in range(3)]
    trace = C[0][0] + C[1][1] + C[2][2]
    S = Matrix([[(trace if i == j else 0.0) - C[i][j] for j in range(3)]
                for i in range(3)])
    normal = Vector((0.31, 0.57, 0.76)).normalized()
    for _ in range(150):
        normal = (S @ normal).normalized()

    hb = main.data.bones[HAND_BONE]
    finger = ((M @ hb.tail_local) - (M @ hb.head_local)).normalized()
    tb = main.data.bones[TOE_BONE]
    facing = (M @ tb.tail_local) - (M @ tb.head_local)
    facing.z = 0.0
    facing.normalize()

    grip = normal.cross(finger).normalized()
    if grip.dot(facing) < 0:
        grip = -grip
    return {"centre": centre, "normal": normal, "finger": finger,
            "facing": facing, "grip": grip}


def place(k, main):
    axes = katana_axes(k)
    frame = hand_frame(main)
    blade = frame["grip"]
    # The blade's flat lies in the palm plane, so the edge is perpendicular to both.
    edge = blade.cross(frame["normal"]).normalized()

    y_img = -blade * axes["sign"]
    z_img = edge * axes["convex_z"]
    x_img = y_img.cross(z_img).normalized()
    z_img = x_img.cross(y_img).normalized()
    R = Matrix(((x_img.x, y_img.x, z_img.x),
                (x_img.y, y_img.y, z_img.y),
                (x_img.z, y_img.z, z_img.z))).to_4x4()

    world = (Matrix.Translation(frame["centre"]) @ R
             @ Matrix.Translation(Vector((0.0, axes["hold_y"], 0.0))).inverted())
    k.parent = None
    k.matrix_world = world
    return world, axes, frame


def bind(k, main, world):
    """One bone, full weight -- the prop follows the joint and cannot deform."""
    for vg in list(k.vertex_groups):
        k.vertex_groups.remove(vg)
    vg = k.vertex_groups.new(name=HAND_BONE)
    vg.add(range(len(k.data.vertices)), 1.0, "REPLACE")
    for m in list(k.modifiers):
        k.modifiers.remove(m)
    mod = k.modifiers.new(name="Armature", type="ARMATURE")
    mod.object = main
    k.parent = main
    k.matrix_parent_inverse = main.matrix_world.inverted()
    k.matrix_world = world
    # merge_animations.py exports exactly the meshes parented to the armature.
    for c in list(k.users_collection):
        c.objects.unlink(k)
    bpy.context.scene.collection.objects.link(k)


def main():
    arm = bpy.data.objects[MAIN_ARM]
    k, saya, how = import_katana()
    report = {"import": how, "verts": len(k.data.vertices)}
    report["material"] = fix_material(k)

    world, axes, frame = place(k, arm)
    bind(k, arm, world)
    bpy.context.view_layer.update()

    if saya is not None:
        saya.hide_set(True)
        saya.hide_viewport = True
        saya.hide_render = True
        report["saya"] = saya.name

    # Verify: the grip axis has to actually pass through the palm.
    a = k.matrix_world @ Vector((0.0, axes["tsuba_y"], 0.0))
    b = k.matrix_world @ Vector((0.0, axes["grip_end_y"], 0.0))
    ab = b - a
    t = max(0.0, min(1.0, (frame["centre"] - a).dot(ab) / ab.length_squared))
    off = (frame["centre"] - (a + ab * t)).length
    if off > 1e-3:
        raise RuntimeError("grip axis misses the palm by %.4f m" % off)

    report["axes"] = {k2: (round(v, 4) if isinstance(v, float) else v)
                      for k2, v in axes.items()}
    report["palm_centre"] = [round(v, 4) for v in frame["centre"]]
    report["blade_dir"] = [round(v, 4) for v in frame["grip"]]
    report["palm_to_grip_axis"] = round(off, 6)
    report["tip_world"] = [round(v, 3) for v in
                           (k.matrix_world @ Vector((0.0, axes["tip_y"], 0.0)))]
    print("ADD_KATANA_RESULT " + repr(report))
    return report
