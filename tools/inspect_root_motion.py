"""Report the root-motion content of a GLB's animation clips.

Standalone (no Blender, no dependencies) -- parses the GLB container and the
accessors it needs directly. Used to verify bake_root_motion.py did what it
claims: joint 0 should be `Root`, the root track should carry the full stride,
and the hips should be left with sway and bob only.

Usage:
    python3 tools/inspect_root_motion.py assets/Alpha_Character.glb
"""

import json
import math
import struct
import sys

COMPONENT = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2),
             5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}
COUNT = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}


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


def translation_track(gltf, buffer, anim, node):
    for channel in anim["channels"]:
        target = channel["target"]
        if target.get("node") == node and target["path"] == "translation":
            sampler = anim["samplers"][channel["sampler"]]
            times = [t[0] for t in read_accessor(gltf, buffer, sampler["input"])]
            values = read_accessor(gltf, buffer, sampler["output"])
            if sampler.get("interpolation") == "CUBICSPLINE":
                values = values[1::3]
            return times, values
    return None, None


def summarize(gltf, buffer, anim, node, label):
    times, values = translation_track(gltf, buffer, anim, node)
    if not values:
        return "%s: no translation track" % label

    origin = values[0]
    peak = max(math.hypot(v[0] - origin[0], v[2] - origin[2]) for v in values)
    net = math.hypot(values[-1][0] - origin[0], values[-1][2] - origin[2])
    bob = max(v[1] for v in values) - min(v[1] for v in values)
    duration = times[-1] if times else 0.0
    speed = net / duration if duration else 0.0
    return ("%s: peakXZ=%6.3f netXZ=%6.3f speed=%5.2f u/s Ybob=%5.3f"
            % (label, peak, net, speed, bob))


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: python3 inspect_root_motion.py <file.glb>")
    path = sys.argv[1]
    gltf, buffer = load_glb(path)

    joints = gltf["skins"][0]["joints"]
    names = [gltf["nodes"][j].get("name", "?") for j in joints]
    print("%s\n  joints=%d  joint[0]=%r  joint[1]=%r"
          % (path, len(joints), names[0], names[1] if len(names) > 1 else None))

    hips_index = next((j for j, n in zip(joints, names) if "Hips" in n), None)

    for anim in gltf.get("animations", []):
        duration = 0.0
        for channel in anim["channels"]:
            sampler = anim["samplers"][channel["sampler"]]
            times = read_accessor(gltf, buffer, sampler["input"])
            if times:
                duration = max(duration, times[-1][0])
        print("\n  %s  (%.2fs)" % (anim.get("name", "?"), duration))
        print("    " + summarize(gltf, buffer, anim, joints[0], "joint0 %-18s" % names[0]))
        if hips_index is not None and hips_index != joints[0]:
            print("    " + summarize(gltf, buffer, anim, hips_index, "hips   %-18s" % "mixamorig:Hips"))


if __name__ == "__main__":
    main()
