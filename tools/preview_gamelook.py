"""Render a character through a replica of the game's character shader.

There is no way to judge a character texture by looking at it in Blender. The
game's skinning.fs is not a PBR shader and does not try to be: it samples the
base colour, applies `AMBIENT + KEY*N.L`, and then raises the result to the
power LIFT. That last step lifts midtones hard, and it is what makes an asset
authored to look right in a normal renderer come out washed out and pale in
game. Judging the texture in Blender's own viewport answers a question nobody
asked.

So this builds an emission material that runs the same arithmetic, disables
Blender's view transform (the game tonemaps nothing), and renders. What comes
out is what the engine will draw, minus shadows.

Kept deliberately in sync with assets/shaders/glsl330/mood_common.glsl and with
kLightDirection in src/Rendering/ShadowMap.cpp -- the constants below are
copies, and if either moves this preview quietly starts lying.

Usage:
    blender --background <file.blend> --python tools/preview_gamelook.py \
        -- <out.png> [object] [--turn]

`--turn` renders three-quarter and back views beside the front one, which is
what catches a texture that only holds up from one angle.

DO NOT SAVE THE .BLEND AFTER RUNNING THIS. build_game_material() rewires the
material's output to an Emission node, in memory, and this script never saves --
but a session that runs it and then saves for some other reason makes that
permanent. glTF then exports the albedo as `emissiveTexture` with
`baseColorFactor [0,0,0,1]`, the game samples only baseColorTexture, and
AssetManager's missing-texture fallback paints the character flat GRAY. It had
happened to pack_miniboss.blend's `Mat`; the repair is to relink the Principled
BSDF, which still holds the correct Base Color, back to Material Output.
"""

import math
import sys

import bpy
from mathutils import Vector

# --- copies of assets/shaders/glsl330/mood_common.glsl ---
#
# The character half of it: MOOD_CHAR_*, plus the two tints the whole scene
# shares. Shadowing is not modelled here (this preview has no depth map), so
# the ambient term is always at full strength -- which is the lit case, and the
# one worth judging a texture in.
AMBIENT = 0.62          # MOOD_CHAR_AMBIENT
KEY = 0.34              # MOOD_CHAR_KEY
LIFT = 0.90             # MOOD_CHAR_LIFT
AMBIENT_TINT = (0.78, 0.94, 0.90)   # MOOD_AMBIENT_TINT
KEY_TINT = (0.90, 0.96, 1.00)       # MOOD_KEY_TINT

# src/Rendering/ShadowMap.cpp kLightDirection, converted from the game's Y-up
# (x, y, z) to Blender's Z-up (x, -z, y).
GAME_LIGHT_DIR = (-0.35, 0.55, -1.0)

BACKDROP = (0.20, 0.20, 0.22, 1.0)


def build_game_material(mat):
    """Replace `mat`'s surface with the shader's own arithmetic."""
    nt = mat.node_tree
    tex = next((n for n in nt.nodes if n.type == "TEX_IMAGE"), None)
    if tex is None:
        raise SystemExit("%s has no image texture to preview" % mat.name)
    out_node = next(n for n in nt.nodes if n.type == "OUTPUT_MATERIAL")

    light = -Vector(GAME_LIGHT_DIR).normalized()

    # The light term is a COLOUR, not a scalar: the shader tints ambient and key
    # separately, and a single grey multiply would hide exactly the thing this
    # preview exists to show -- what the cool cast does to a warm texture.
    #
    # Ambient carries no shadow term here, so it is constant and folds into one
    # precomputed colour. Only the key varies across the surface.
    amb_col = tuple(AMBIENT * c for c in AMBIENT_TINT)

    geo = nt.nodes.new("ShaderNodeNewGeometry")
    dot = nt.nodes.new("ShaderNodeVectorMath")
    dot.operation = "DOT_PRODUCT"
    dot.inputs[1].default_value = (light.x, light.y, light.z)
    clamp = nt.nodes.new("ShaderNodeMath")
    clamp.operation = "MAXIMUM"
    clamp.inputs[1].default_value = 0.0
    key = nt.nodes.new("ShaderNodeMath")
    key.operation = "MULTIPLY"
    key.inputs[1].default_value = KEY

    # key scalar -> key colour. Linking a Value socket into a Color socket
    # broadcasts it to grey, which is what makes this a plain tint multiply.
    key_col = nt.nodes.new("ShaderNodeMixRGB")
    key_col.blend_type = "MULTIPLY"
    key_col.inputs["Fac"].default_value = 1.0
    key_col.inputs["Color1"].default_value = (KEY_TINT[0], KEY_TINT[1], KEY_TINT[2], 1.0)

    light_col = nt.nodes.new("ShaderNodeMixRGB")
    light_col.blend_type = "ADD"
    light_col.inputs["Fac"].default_value = 1.0
    light_col.inputs["Color1"].default_value = (amb_col[0], amb_col[1], amb_col[2], 1.0)

    shade = nt.nodes.new("ShaderNodeMixRGB")
    shade.blend_type = "MULTIPLY"
    shade.inputs["Fac"].default_value = 1.0
    lift = nt.nodes.new("ShaderNodeGamma")
    lift.inputs["Gamma"].default_value = LIFT
    emit = nt.nodes.new("ShaderNodeEmission")

    nt.links.new(geo.outputs["Normal"], dot.inputs[0])
    nt.links.new(dot.outputs["Value"], clamp.inputs[0])
    nt.links.new(clamp.outputs["Value"], key.inputs[0])
    nt.links.new(key.outputs["Value"], key_col.inputs["Color2"])
    nt.links.new(key_col.outputs["Color"], light_col.inputs["Color2"])
    nt.links.new(tex.outputs["Color"], shade.inputs["Color1"])
    nt.links.new(light_col.outputs["Color"], shade.inputs["Color2"])
    nt.links.new(shade.outputs["Color"], lift.inputs["Color"])
    nt.links.new(lift.outputs["Color"], emit.inputs["Color"])
    nt.links.new(emit.outputs["Emission"], out_node.inputs["Surface"])


def main():
    args = sys.argv[sys.argv.index("--") + 1:]
    out_path = args[0]
    turn = "--turn" in args
    named = [a for a in args[1:] if not a.startswith("--")]
    obj_name = named[0] if named else None

    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    if obj_name:
        obj = bpy.data.objects[obj_name]
    else:
        obj = max(meshes, key=lambda o: len(o.data.vertices))

    for slot in obj.material_slots:
        if slot.material:
            build_game_material(slot.material)

    scene = bpy.context.scene
    # The engine writes straight to the framebuffer with no tonemap, so any
    # view transform here would be showing a picture the game cannot produce.
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    scene.render.film_transparent = False
    scene.render.image_settings.file_format = "PNG"

    world = scene.world or bpy.data.worlds.new("World")
    scene.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    bg.inputs[0].default_value = BACKDROP
    bg.inputs[1].default_value = 1.0

    for arm in [o for o in bpy.data.objects if o.type == "ARMATURE"]:
        if arm.animation_data:
            arm.animation_data.action = None
    bpy.context.view_layer.update()

    dg = bpy.context.evaluated_depsgraph_get()
    ev = obj.evaluated_get(dg)
    me = ev.to_mesh()
    pts = [ev.matrix_world @ v.co for v in me.vertices]
    lo = Vector((min(p.x for p in pts), min(p.y for p in pts),
                 min(p.z for p in pts)))
    hi = Vector((max(p.x for p in pts), max(p.y for p in pts),
                 max(p.z for p in pts)))
    ev.to_mesh_clear()
    centre = (lo + hi) * 0.5
    height = hi.z - lo.z

    cam = bpy.data.objects.get("PreviewCam")
    if cam is None:
        cam = bpy.data.objects.new("PreviewCam", bpy.data.cameras.new("PreviewCam"))
        bpy.context.collection.objects.link(cam)
    cam.data.lens = 70
    scene.camera = cam
    scene.render.resolution_x = 900
    scene.render.resolution_y = 1500

    dist = height * 2.25
    angles = (0.0, 40.0, 180.0) if turn else (0.0,)
    for i, deg in enumerate(angles):
        a = math.radians(deg)
        cam.location = (centre.x + math.sin(a) * dist,
                        centre.y - math.cos(a) * dist,
                        centre.z)
        cam.rotation_euler = (math.radians(90), 0, a)
        scene.render.filepath = (out_path if len(angles) == 1
                                 else out_path.replace(".png", "_%d.png" % i))
        bpy.ops.render.render(write_still=True)
        print("PREVIEW_WROTE %s" % scene.render.filepath)


if __name__ == "__main__":
    main()
