"""Relink the kimono enemy's missing textures and correct their colour spaces.

    blender -b ~/Documents/3D/Model/Kimono_enemy/GH10.blend \
        --python tools/link_kimono_textures.py

Reads GH10.blend and writes GH10_textured.blend beside it. The source file is
never modified.

What was wrong
--------------
The materials were already wired correctly -- albedo into Base Color, normal
into a Normal Map node and on into the Principled's Normal -- but six image
datablocks pointed at paths that do not exist on this machine:

    c7409_body_a.png    c7409_kimono_a.png    c7409_waraji_a.png
    c7409_body_n.png    c7409_kimono_n.png    c7409_waraji_n.png

So this script relinks rather than rebuilds. Rebuilding the materials would
throw away node wiring that is already right, and there is a lot of it: the
kimono albedo is shared by two materials (`_28__Phantom_Hakama` and
`Material__0`) through one datablock, so repointing that datablock fixes both.

Which file is which
-------------------
Identified by LOOKING at each image, not by trusting the numbering:

    texture1  1024^2  the body  -- torso, limbs, foot soles, toenails
    texture2  2048^2  the kimono -- panels, obi, hakama, crane embroidery
    texture3   512^2  the waraji -- woven straw sandal soles

The normals pair with their albedo by index, which is how they were supplied.
They are NOT the same resolution as their albedo (512/2048/256 against
1024/2048/512); that is normal for this kind of pack and not a mismatch.

Colour space, which was wrong on all three normals
--------------------------------------------------
Every normal map arrived tagged as colour data -- two as `Linear CIE-XYZ E` and
one as `sRGB`. A normal map is not a colour: its RGB channels are a vector, and
running them through a transfer curve bends that vector. The surface still
lights, which is what makes this the kind of defect that ships -- it just lights
wrongly, with the bumps too shallow and skewed. All three are set to Non-Color
here, and the albedos are asserted to be sRGB.

Packing
-------
The images are packed into the .blend. The originals live in a folder beside it
and the previous set of paths in this file broke precisely because they were
external, so the result is made self-contained rather than made to depend on a
second relative path surviving.
"""

import os
import sys

import bpy

SRC_DIR = "/Users/long/Documents/3D/Model/Kimono_enemy"
OUT = os.path.join(SRC_DIR, "GH10_textured.blend")

# image datablock -> (file, role, expected pixel size)
LINKS = {
    "c7409_body_a.png":   ("texture1.jpeg", "albedo", 1024),
    "c7409_body_n.png":   ("normal1.jpeg",  "normal",  512),
    "c7409_kimono_a.png": ("texture2.jpeg", "albedo", 2048),
    "c7409_kimono_n.png": ("normal2.jpeg",  "normal", 2048),
    "c7409_waraji_a.png": ("texture3.jpeg", "albedo",  512),
    "c7409_waraji_n.png": ("normal3.jpeg",  "normal",  256),
}

COLORSPACE = {"albedo": "sRGB", "normal": "Non-Color"}


def main():
    report, problems = {}, []

    for name, (filename, role, expect) in LINKS.items():
        img = bpy.data.images.get(name)
        if img is None:
            problems.append("no image datablock named %r" % name)
            continue

        path = os.path.join(SRC_DIR, filename)
        if not os.path.exists(path):
            problems.append("%s: no such file" % path)
            continue

        was_space = img.colorspace_settings.name
        img.filepath = path
        img.source = "FILE"
        img.reload()

        if tuple(img.size) == (0, 0):
            problems.append("%s did not load from %s" % (name, filename))
            continue
        if img.size[0] != expect:
            problems.append("%s loaded at %dpx, expected %d -- is the mapping "
                            "right?" % (name, img.size[0], expect))

        img.colorspace_settings.name = COLORSPACE[role]
        img.pack()

        report[name] = {
            "file": filename, "role": role, "size": tuple(img.size),
            "colorspace": "%s -> %s" % (was_space, img.colorspace_settings.name),
            "packed": bool(img.packed_file),
        }

    # Every material that consumes these, and whether it is now fully fed.
    consumers = {}
    for mat in bpy.data.materials:
        if not mat.use_nodes:
            continue
        used = [n.image.name for n in mat.node_tree.nodes
                if n.type == "TEX_IMAGE" and n.image]
        if any(u in LINKS for u in used):
            consumers[mat.name] = {
                "users": mat.users,
                "images": used,
                "all_loaded": all(tuple(bpy.data.images[u].size) != (0, 0)
                                  for u in used),
            }

    # Anything still missing anywhere in the file, so it is said out loud rather
    # than discovered as a grey patch on the model.
    still_missing = sorted(
        i.name for i in bpy.data.images
        if i.source == "FILE" and not i.packed_file and tuple(i.size) == (0, 0))

    print("LINK_KIMONO " + repr({
        "linked": report,
        "consumers": consumers,
        "still_missing": still_missing,
        "problems": problems,
    }))

    if problems:
        raise RuntimeError("texture linking failed: %s" % problems)

    bpy.ops.wm.save_as_mainfile(filepath=OUT)
    print("SAVED " + OUT)


if __name__ == "__main__":
    main()
