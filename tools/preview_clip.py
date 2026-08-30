"""Pose the miniboss through a clip and render a strip of frames.

    blender -b ~/Documents/3D/Model/pack_miniboss.blend \
        --python tools/preview_clip.py -- out.png "great sword blocking.fbx" [n_frames] [yaw_deg]

The clip may be an .fbx path (absolute, or a name inside SEARCH_DIRS) or the
name of an action already in the file. Either way it is transplanted onto
`Armature` and the character is rendered through tools/preview_gamelook.py's
replica of the game shader, so what comes out is what the engine will draw.

Two things this exists to avoid guessing about:

  * whether a downloaded clip actually reads as the thing its filename claims.
    Mixamo names are approximate and the pack ships five "impact" and three
    "blocking" variants; the only way to tell them apart is to look at them on
    THIS character, holding THIS 2.5 m sword.
  * whether a clip's rest pose is close enough to this rig's to transplant. The
    deviation is printed per clip. Mixamo's packs agree to a median of 0.00 deg
    on everything but the ankles, which sit about 21 deg apart -- worth knowing
    before blaming an animation for feet that point wrong.

Frames are sampled across the INTERIOR of the range. The last keyframe of a
raylib clip wraps to the first, so the final frame is never what the runtime
plays and never worth looking at.
"""

import math
import os
import sys

import bpy
import numpy as np
from mathutils import Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import preview_gamelook as P

MAIN_ARM = "Armature"
BODY = "mesh"
SWORD = "MiniBoss_Sword"
SEARCH_DIRS = [
    "/Users/long/Documents/3D/Model/animation pack mini boss",
    "/Users/long/Documents/3D/Model/Great Sword Pack",
]
TILE = (460, 760)


def resolve(spec):
    """An action for `spec`, importing an FBX for it if that is what it names."""
    if spec in bpy.data.actions:
        return bpy.data.actions[spec], None, spec
    path = spec if os.path.isabs(spec) else None
    if path is None:
        for d in SEARCH_DIRS:
            cand = os.path.join(d, spec)
            if os.path.exists(cand):
                path = cand
                break
    if path is None or not os.path.exists(path):
        raise SystemExit("no action or file named %r" % spec)

    before = set(o.name for o in bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=path)
    new = [bpy.data.objects[n] for n in set(o.name for o in bpy.data.objects) - before]
    src = next(o for o in new if o.type == "ARMATURE")
    act = src.animation_data.action
    slot = src.animation_data.action_slot
    act.use_fake_user = True

    arm = bpy.data.objects[MAIN_ARM]
    devs = []
    for b in src.data.bones:
        o = arm.data.bones.get(b.name)
        if not o:
            continue
        u = (src.matrix_world @ b.tail_local) - (src.matrix_world @ b.head_local)
        v = (arm.matrix_world @ o.tail_local) - (arm.matrix_world @ o.head_local)
        if u.length > 1e-9 and v.length > 1e-9:
            devs.append(math.degrees(u.angle(v)))
    print("REST_DEVIATION median %.2f deg  max %.2f deg over %d bones"
          % (np.median(devs), max(devs), len(devs)))
    for o in new:
        bpy.data.objects.remove(o, do_unlink=True)
    return act, slot, os.path.basename(path)


def main():
    args = sys.argv[sys.argv.index("--") + 1:]
    out_path, spec = args[0], args[1]
    count = int(args[2]) if len(args) > 2 else 3
    yaw = float(args[3]) if len(args) > 3 else 35.0

    action, slot, label = resolve(spec)
    arm = bpy.data.objects[MAIN_ARM]
    targets = [bpy.data.objects[BODY], bpy.data.objects[SWORD]]

    seen = set()
    for o in targets:
        for s in o.material_slots:
            if s.material and s.material.name not in seen:
                seen.add(s.material.name)
                P.build_game_material(s.material)

    sc = bpy.context.scene
    sc.view_settings.view_transform = "Standard"
    sc.view_settings.look = "None"
    sc.render.film_transparent = False
    sc.render.image_settings.file_format = "PNG"
    w = sc.world or bpy.data.worlds.new("World")
    sc.world = w
    w.use_nodes = True
    w.node_tree.nodes["Background"].inputs[0].default_value = P.BACKDROP

    if arm.animation_data is None:
        arm.animation_data_create()
    arm.animation_data.action = action
    if slot is not None:
        arm.animation_data.action_slot = slot

    f0, f1 = (int(v) for v in action.frame_range)
    # Interior only: the final keyframe wraps to the first at runtime.
    span = max(f1 - 1 - f0, 0)
    frames = [f0 + round(span * i / max(count - 1, 1)) for i in range(count)]

    cam = bpy.data.objects.new("ClipCam", bpy.data.cameras.new("ClipCam"))
    bpy.context.collection.objects.link(cam)
    sc.camera = cam
    cam.data.lens = 50
    sc.render.resolution_x, sc.render.resolution_y = TILE

    # Framed once, over every frame of the clip, so the character does not
    # rescale between tiles and a stride reads as a stride.
    lo = Vector((1e9, 1e9, 1e9))
    hi = Vector((-1e9, -1e9, -1e9))
    for f in frames:
        sc.frame_set(f)
        dg = bpy.context.evaluated_depsgraph_get()
        for o in targets:
            ev = o.evaluated_get(dg)
            me = ev.to_mesh()
            for v in me.vertices:
                p = ev.matrix_world @ v.co
                lo = Vector((min(lo.x, p.x), min(lo.y, p.y), min(lo.z, p.z)))
                hi = Vector((max(hi.x, p.x), max(hi.y, p.y), max(hi.z, p.z)))
            ev.to_mesh_clear()
    centre = (lo + hi) * 0.5
    reach = max(hi - lo)

    tiles = []
    a = math.radians(yaw)
    for i, f in enumerate(frames):
        sc.frame_set(f)
        cam.location = (centre.x + math.sin(a) * reach * 1.6,
                        centre.y - math.cos(a) * reach * 1.6, centre.z)
        cam.rotation_euler = (math.radians(90), 0, a)
        tmp = "%s.f%03d.png" % (out_path, f)
        sc.render.filepath = tmp
        bpy.ops.render.render(write_still=True)
        tiles.append(tmp)

    strip = None
    for i, t in enumerate(tiles):
        img = bpy.data.images.load(t)
        buf = np.empty(len(img.pixels), dtype=np.float32)
        img.pixels.foreach_get(buf)
        h, wid = img.size[1], img.size[0]
        px = buf.reshape(h, wid, 4)
        if strip is None:
            strip = np.zeros((h, wid * len(tiles), 4), dtype=np.float32)
        strip[:, i * wid:(i + 1) * wid] = px
        bpy.data.images.remove(img)
        os.remove(t)

    out = bpy.data.images.new("strip", strip.shape[1], strip.shape[0], alpha=True)
    out.pixels.foreach_set(strip.reshape(-1))
    out.filepath_raw = out_path
    out.file_format = "PNG"
    out.save()
    print("PREVIEW_CLIP %s -> %s frames=%s of %d..%d" % (label, out_path, frames, f0, f1))


if __name__ == "__main__":
    main()
