#!/usr/bin/env python3
"""Remove unreferenced animation clips from a .glb, in place of a re-export.

Why surgery and not Blender: re-exporting through Blender would re-run every
trap in the root-motion bake (export_def_bones dropping the weightless `Root`
bone from skin.joints, the alphabetical clip reorder, the hips re-key). This
script never touches the node hierarchy, the skin, the meshes or the materials
-- it deletes `animations` entries, garbage-collects the accessors and
bufferViews that only those entries referenced, and repacks the binary chunk.
Everything the renderer reads survives byte-for-byte.

    python3 tools/strip_unused_clips.py in.glb out.glb --keep Idle Walk Run ...
    python3 tools/strip_unused_clips.py in.glb out.glb --keep-from clips.txt
"""
import argparse, json, struct, sys

GLB_MAGIC, JSON_CHUNK, BIN_CHUNK = 0x46546C67, 0x4E4F534A, 0x004E4942


def read_glb(path):
    with open(path, "rb") as f:
        magic, version, _ = struct.unpack("<III", f.read(12))
        if magic != GLB_MAGIC:
            sys.exit(f"{path}: not a GLB")
        js, binary = None, b""
        while True:
            hdr = f.read(8)
            if len(hdr) < 8:
                break
            length, ctype = struct.unpack("<II", hdr)
            data = f.read(length)
            if ctype == JSON_CHUNK:
                js = json.loads(data.decode("utf-8"))
            elif ctype == BIN_CHUNK:
                binary = data
    return js, binary


def write_glb(path, js, binary):
    jb = json.dumps(js, separators=(",", ":")).encode("utf-8")
    jb += b" " * ((4 - len(jb) % 4) % 4)
    binary += b"\0" * ((4 - len(binary) % 4) % 4)
    total = 12 + 8 + len(jb) + (8 + len(binary) if binary else 0)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", GLB_MAGIC, 2, total))
        f.write(struct.pack("<II", len(jb), JSON_CHUNK)); f.write(jb)
        if binary:
            f.write(struct.pack("<II", len(binary), BIN_CHUNK)); f.write(binary)


def collect_bufferview_refs(node, out, skip_key=None):
    """Every `bufferView` index anywhere in the JSON, so an unknown extension
    that points at one keeps it alive rather than losing its bytes silently."""
    if isinstance(node, dict):
        for k, v in node.items():
            if k == skip_key:
                continue
            if k == "bufferView" and isinstance(v, int):
                out.add(v)
            else:
                collect_bufferview_refs(v, out, skip_key)
    elif isinstance(node, list):
        for v in node:
            collect_bufferview_refs(v, out, skip_key)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src"); ap.add_argument("dst")
    ap.add_argument("--keep", nargs="*", default=[])
    ap.add_argument("--keep-from")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    keep = set(a.keep)
    if a.keep_from:
        keep |= {l.strip() for l in open(a.keep_from) if l.strip() and not l.startswith("#")}

    js, binary = read_glb(a.src)
    anims = js.get("animations", [])
    have = {x.get("name") for x in anims}
    missing = keep - have
    if missing:
        sys.exit(f"{a.src}: --keep names not present: {sorted(missing)}")

    kept_anims = [x for x in anims if x.get("name") in keep]
    dropped = [x.get("name") for x in anims if x.get("name") not in keep]
    print(f"{a.src}: {len(anims)} clips -> keeping {len(kept_anims)}, dropping {len(dropped)}")
    if a.dry_run:
        print("  dropping:", ", ".join(dropped)); return

    js["animations"] = kept_anims
    if not kept_anims:
        js.pop("animations", None)

    # --- accessors still referenced -------------------------------------
    # Everything outside `animations` is kept wholesale; only the samplers of
    # the surviving clips add to it. Anything left over belonged solely to a
    # dropped clip.
    keep_acc = set()

    def add(i):
        if isinstance(i, int):
            keep_acc.add(i)

    for mesh in js.get("meshes", []):
        for prim in mesh.get("primitives", []):
            for v in prim.get("attributes", {}).values():
                add(v)
            add(prim.get("indices"))
            for t in prim.get("targets", []) or []:
                for v in t.values():
                    add(v)
    for skin in js.get("skins", []):
        add(skin.get("inverseBindMatrices"))
    for anim in kept_anims:
        for s in anim.get("samplers", []):
            add(s.get("input")); add(s.get("output"))

    old_acc = js.get("accessors", [])
    acc_order = sorted(keep_acc)
    acc_map = {old: new for new, old in enumerate(acc_order)}

    # --- bufferViews still referenced -----------------------------------
    keep_bv = set()
    for i in acc_order:
        acc = old_acc[i]
        if isinstance(acc.get("bufferView"), int):
            keep_bv.add(acc["bufferView"])
        sp = acc.get("sparse")
        if sp:
            for part in ("indices", "values"):
                if isinstance(sp.get(part, {}).get("bufferView"), int):
                    keep_bv.add(sp[part]["bufferView"])
    # images, and any extension elsewhere in the file (accessors excluded --
    # they were just resolved precisely above)
    scratch = {k: v for k, v in js.items() if k not in ("accessors", "animations")}
    collect_bufferview_refs(scratch, keep_bv)

    old_bv = js.get("bufferViews", [])
    bv_order = sorted(keep_bv)
    bv_map = {old: new for new, old in enumerate(bv_order)}

    # --- repack the binary chunk ----------------------------------------
    new_bin = bytearray()
    new_bvs = []
    for old in bv_order:
        bv = dict(old_bv[old])
        off, ln = bv.get("byteOffset", 0), bv["byteLength"]
        pad = (4 - len(new_bin) % 4) % 4
        new_bin += b"\0" * pad
        bv["byteOffset"] = len(new_bin)
        new_bin += binary[off:off + ln]
        bv["buffer"] = 0
        new_bvs.append(bv)

    new_accs = []
    for i in acc_order:
        acc = dict(old_acc[i])
        if isinstance(acc.get("bufferView"), int):
            acc["bufferView"] = bv_map[acc["bufferView"]]
        if acc.get("sparse"):
            sp = json.loads(json.dumps(acc["sparse"]))
            for part in ("indices", "values"):
                if isinstance(sp.get(part, {}).get("bufferView"), int):
                    sp[part]["bufferView"] = bv_map[sp[part]["bufferView"]]
            acc["sparse"] = sp
        new_accs.append(acc)

    # --- remap every accessor index -------------------------------------
    for mesh in js.get("meshes", []):
        for prim in mesh.get("primitives", []):
            prim["attributes"] = {k: acc_map[v] for k, v in prim.get("attributes", {}).items()}
            if isinstance(prim.get("indices"), int):
                prim["indices"] = acc_map[prim["indices"]]
            if prim.get("targets"):
                prim["targets"] = [{k: acc_map[v] for k, v in t.items()} for t in prim["targets"]]
    for skin in js.get("skins", []):
        if isinstance(skin.get("inverseBindMatrices"), int):
            skin["inverseBindMatrices"] = acc_map[skin["inverseBindMatrices"]]
    for anim in kept_anims:
        for s in anim.get("samplers", []):
            s["input"] = acc_map[s["input"]]
            s["output"] = acc_map[s["output"]]
    # images and anything else that named a bufferView
    def remap_bv(node):
        if isinstance(node, dict):
            for k, v in node.items():
                if k == "bufferView" and isinstance(v, int):
                    node[k] = bv_map[v]
                else:
                    remap_bv(v)
        elif isinstance(node, list):
            for v in node:
                remap_bv(v)
    for key in ("images", "extensions", "nodes", "materials", "textures"):
        if key in js:
            remap_bv(js[key])

    js["accessors"] = new_accs
    js["bufferViews"] = new_bvs
    js["buffers"] = [{"byteLength": len(new_bin)}]

    write_glb(a.dst, js, bytes(new_bin))
    print(f"  accessors {len(old_acc)} -> {len(new_accs)}, "
          f"bufferViews {len(old_bv)} -> {len(new_bvs)}, "
          f"bin {len(binary)/1e6:.1f} MB -> {len(new_bin)/1e6:.1f} MB")


if __name__ == "__main__":
    main()
