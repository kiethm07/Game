"""Set the ground palette on a built level .blend.

One place, one number, and every downstream thing follows from it: the terrain
takes this colour directly, and tools/scatter_grass.py *reads it back off the
material* to colour the grass, so the two cannot be typed differently and
cannot drift apart when the palette is retuned.

Which is the whole reason this is its own tool rather than a constant in the
scatter. Grass is only in phase 1, but the ground is in all three phases, and a
campaign whose meadows change hue at a phase boundary looks worse than any one
of them looks wrong. Run it on every phase; run the scatter on the one that
needs grass.

WHAT THE NUMBERS MEAN

Nothing in this engine's shader chain does an sRGB conversion. level.fs writes
`texelColor.rgb*light*colDiffuse` straight out, `colDiffuse` is the glTF
`baseColorFactor`, and `light` on lit flat ground is about 1.0 -- so the triple
set here is very nearly the literal framebuffer colour. Pick it by eye against a
reference image, not by converting from a hex code.

The authored value was (0.191, 0.410, 0.221), a dark saturated green. The
default below is brighter and further toward yellow, which is the Breath of the
Wild meadow palette this map is being pulled toward.

Usage:
    blender --background source/levels/<phase>.blend \
        --python tools/tint_ground.py -- [--colour R,G,B] [--path-colour R,G,B]
        [--out <path>] [--dry-run]
"""

import argparse
import os
import sys

import bpy

# The terrain material. Every landscape mesh in these levels shares it, the
# BACKDROP included, so one assignment re-tints the whole horizon with the
# ground the player is standing on.
GROUND_MATERIAL = "grass"

# The trail. Optional, and left alone unless asked for -- but worth checking
# after a ground re-tint, because the authored (0.509, 0.445, 0.189) was chosen
# to read against a much darker green than the one below.
PATH_MATERIAL = "path"

GROUND_COLOUR = (0.46, 0.68, 0.24)


class TintError(Exception):
    """The material is not in the shape this can edit."""


def parse_colour(text):
    parts = text.split(",")
    if len(parts) != 3:
        raise TintError("expected R,G,B, got %r" % text)
    try:
        values = [float(p) for p in parts]
    except ValueError:
        raise TintError("expected three numbers, got %r" % text)
    for v in values:
        if not 0.0 <= v <= 1.0:
            raise TintError("%r is outside 0..1; these are linear factors, "
                            "not 0-255 bytes" % text)
    return tuple(values)


def base_colour_input(name):
    """The Base Color socket of `name`'s Principled BSDF.

    Refuses a linked socket rather than quietly overwriting a node graph. A
    linked Base Color is also what export_level.py's flatten_procedural_colours
    reduces to a flat value at export time by its own rule, so writing here
    would leave two different answers to the same question.
    """
    material = bpy.data.materials.get(name)
    if material is None:
        raise TintError("no material named %r in this .blend" % name)
    if not material.use_nodes or material.node_tree is None:
        raise TintError("%r has no node tree" % name)
    bsdf = next((n for n in material.node_tree.nodes
                 if n.type == "BSDF_PRINCIPLED"), None)
    if bsdf is None:
        raise TintError("%r has no Principled BSDF" % name)
    socket = bsdf.inputs.get("Base Color")
    if socket is None:
        raise TintError("%r's Principled BSDF has no Base Color" % name)
    if socket.links:
        raise TintError(
            "%r drives Base Color from a %s node. Re-tinting would leave the "
            "node graph and the flat value disagreeing, and export_level.py "
            "reduces the graph to its own answer. Unlink it first."
            % (name, socket.links[0].from_node.type))
    return socket


def tint(name, colour):
    socket = base_colour_input(name)
    before = tuple(round(v, 3) for v in socket.default_value)[:3]
    socket.default_value = (colour[0], colour[1], colour[2], 1.0)
    return before, colour


def parse_args(argv):
    argv = argv[argv.index("--") + 1:] if "--" in argv else []
    parser = argparse.ArgumentParser(prog="tint_ground.py")
    parser.add_argument("--colour", default=None,
                        help="ground colour as R,G,B in 0..1 (default %s)"
                             % ",".join("%.2f" % c for c in GROUND_COLOUR))
    parser.add_argument("--path-colour", default=None,
                        help="also re-tint the trail, as R,G,B in 0..1")
    parser.add_argument("--out", default=None,
                        help="where to save (defaults to the input .blend)")
    parser.add_argument("--dry-run", action="store_true",
                        help="report but do not write the .blend")
    return parser.parse_args(argv)


def main():
    args = parse_args(sys.argv)
    colour = parse_colour(args.colour) if args.colour else GROUND_COLOUR

    changes = [(GROUND_MATERIAL,) + tint(GROUND_MATERIAL, colour)]
    if args.path_colour:
        path = parse_colour(args.path_colour)
        changes.append((PATH_MATERIAL,) + tint(PATH_MATERIAL, path))

    for name, before, after in changes:
        print("[tint_ground] %-6s (%.3f, %.3f, %.3f) -> (%.3f, %.3f, %.3f)"
              % ((name,) + before + after))

    if args.dry_run:
        print("[tint_ground] dry run -- not saved")
        return

    out = args.out or bpy.data.filepath
    if not out:
        raise TintError("no --out and the .blend has no path to save back to.")
    bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(out))
    print("[tint_ground] saved  %s" % out)


if __name__ == "__main__":
    try:
        main()
    except TintError as error:
        raise SystemExit("[tint_ground] %s" % error)
