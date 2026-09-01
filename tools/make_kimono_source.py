"""Reduce GH10_textured.blend to one clean, fully-skinned kimono character.

    blender -b ~/Documents/3D/Model/Kimono_enemy/GH10_textured.blend \
        --python tools/make_kimono_source.py

Writes ~/Documents/3D/Model/kimono.blend. The source file is never modified.
This is step 1 of tools/rebuild_kimono.sh; tools/retarget_kimono.py appends
what this leaves behind.

The whole character is in the file twice
---------------------------------------
Two collections, `Collection` and `Backup`, each holding a complete copy: one
106-bone armature, the same ten skinned body meshes (16,426 vertices), and the
same 183 loose head and hair pieces (75,325 vertices). Same shape signature
object for object. `Backup` is hidden in the view layer, which is what makes
`Collection` the working copy -- so `Backup` is what gets deleted.

Hidden is not excluded, and that distinction is the trap. `Backup` is still in
the depsgraph and every exporter still walks it, so leaving it costs a doubled
character rather than nothing.

The two copies are NOT coincident -- `Backup` sits about 2 cm off -- so
comparing objects by world bounding box pairs the ten body meshes and finds all
366 head pieces distinct. That test says nothing about which are duplicates.
The collections do, so this works on collections.

The head and hair are not skinned at all
----------------------------------------
183 loose objects -- face, glasses, 179 hair cards, hairtie -- with no parent,
no armature modifier and no vertex groups. They sit at the head by world
position alone, which looks correct in the bind pose and detaches the moment
anything animates. They are joined into one mesh and RIGIDLY bound to `Head`,
weight 1.0. That is the whole binding: a ponytail rigid to the skull swings
with it and has no secondary motion, which is what this game does everywhere
else (the miniboss's greatsword is one bone too).

One sword, and it has to be the one on `L_Katana`
------------------------------------------------
The character ships two identical 301-vertex blades and a scabbard: `R_Katana`
off `R_Hand`, and `L_Katana` off `L_Saya` off `L_Hand` -- an iaido/dual-wield
set. The moveset being built here is a two-handed nodachi, where the greatsword
clips bring both hands onto one grip, so a second blade and a scabbard riding
the other hand would pass through the first and through the arm. One survives.

WHICH one is not a coin toss, because **this rig's side names are mirrored
relative to Mixamo's**. Both rigs face -Y (measured off the toes, which sit at
lower Y than the ankle on both), and with the same facing Mixamo's `LeftFoot`
sits at x=+0.098 while this rig's `L_Foot` sits at x=-0.074. So `L_*` here is
Mixamo's `Right*`. tools/retarget_kimono.py maps it that way; see its docstring.

Mixamo's greatsword clips grip with `mixamorig:RightHand` (the same hand
tools/add_miniboss_sword.py binds the Blood Knight's greatsword to), which is
this rig's `L_Hand`. So the blade that must survive is `L_Katana`, and the one
dropped is `R_Katana`. Keeping the other would put the sword in the supporting
hand and leave the gripping fist closed on nothing.

The `L_Saya` BONE is kept even though its scabbard mesh is not: `L_Katana`
hangs off it, and it is the offset that seats the blade in the fist.

The hair textures do not exist on this machine
----------------------------------------------
`01 - Default`, on 179 of the hair cards, points at three images under
`//hair-cards-fbx/textures/` that are not here and are not anywhere under
~/Documents/3D:

    HSD_Skecthfab_RGBMask.png    -> Base Color, through a Mix
    HSD_Skecthfab_Mask.png       -> Alpha, with blend_method HASHED
    HSD_Skecthfab_NormalMap.png  -> Normal

An unloaded image does not export as nothing: it exports as a material still
claiming an alpha mask, which is how you get hair that is invisible in one
renderer and solid magenta in the next. The three links are cut and the material
made flat, opaque and dark. The strands lose their tip fade but not their shape
-- these are modelled ribbons averaging ~410 vertices each, not alpha quads.
Drop the real textures in and delete FLAT_HAIR to get the authored look back.
"""

import os

import bpy

OUT = os.path.expanduser("~/Documents/3D/Model/kimono.blend")

# The two complete copies. `KEEP` is the one visible in the view layer.
KEEP_COLLECTION = "Collection"
DROP_COLLECTION = "Backup"

# The bone the joined head and hair are rigidly bound to.
HEAD_BONE = "Head"

# The blade that goes, and the meshes that go with it. `L_Saya`'s scabbard mesh
# is dropped but its bone is not -- see the module docstring.
DROP_BONES = ("R_Katana",)
DROP_MESH_BONES = ("R_Katana", "L_Saya")

# The hair material, and what to make it when its textures are missing.
# Linear, not sRGB: glTF's baseColorFactor is linear and raylib reads it
# straight into a byte colour, so 0.055 arrives as 14/255 and reads as a hole
# in the head. These values land around 45/38/33, a dark warm brown.
HAIR_MATERIAL = "01 - Default"
FLAT_HAIR = (0.176, 0.149, 0.133, 1.0)


def drop_backup_collection():
    """Delete the `Backup` copy of the character, objects and all.

    Asserts rather than tolerates a miss: if this collection is ever renamed
    upstream, silently doubling the character is a far worse outcome than a
    build that stops and says so.
    """
    doomed = bpy.data.collections.get(DROP_COLLECTION)
    if doomed is None:
        raise RuntimeError(
            "no %r collection -- has the source file been restructured? Check "
            "which collections hold copies before continuing, or this will "
            "carry the character twice." % DROP_COLLECTION)

    names = [o.name for o in doomed.objects]
    for o in list(doomed.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    bpy.data.collections.remove(doomed)
    return names


def drop_stray_empties():
    """The parentless EMPTYs left over from whatever imported this."""
    names = []
    for o in list(bpy.data.objects):
        if o.type == "EMPTY" and not o.children:
            names.append(o.name)
            bpy.data.objects.remove(o, do_unlink=True)
    return names


def the_armature():
    arms = [o for o in bpy.data.objects if o.type == "ARMATURE"]
    if len(arms) != 1:
        raise RuntimeError("expected exactly one armature, found %d: %s"
                           % (len(arms), [a.name for a in arms]))
    return arms[0]


def drop_second_sword(arm):
    """Delete the left katana and scabbard, meshes first and then their bones.

    Meshes are found by which bone carries their weight rather than by name:
    the object names are import serial numbers (`c7400.009`) and say nothing
    about what they are, while the binding does.
    """
    dropped_meshes = []
    for obj in list(bpy.data.objects):
        if obj.type != "MESH" or obj.parent is not arm:
            continue
        groups = {vg.name for vg in obj.vertex_groups}
        if groups and groups <= set(DROP_MESH_BONES):
            dropped_meshes.append(obj.name)
            bpy.data.objects.remove(obj, do_unlink=True)

    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="EDIT")
    dropped_bones = []
    for name in DROP_BONES:
        eb = arm.data.edit_bones.get(name)
        if eb is None:
            continue
        if eb.children:
            raise RuntimeError("%s has children (%s); removing it would orphan "
                               "them" % (name, [c.name for c in eb.children]))
        dropped_bones.append(name)
        arm.data.edit_bones.remove(eb)
    bpy.ops.object.mode_set(mode="OBJECT")
    return dropped_meshes, dropped_bones


def flatten_hair():
    """Cut the three dead texture links and make the hair flat and opaque."""
    mat = bpy.data.materials.get(HAIR_MATERIAL)
    if mat is None:
        return None
    bsdf = next((n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED"),
                None)
    if bsdf is None:
        return None

    cut = []
    for socket in ("Base Color", "Alpha", "Normal"):
        inp = bsdf.inputs[socket]
        for link in list(inp.links):
            cut.append(socket)
            mat.node_tree.links.remove(link)

    bsdf.inputs["Base Color"].default_value = FLAT_HAIR
    bsdf.inputs["Alpha"].default_value = 1.0
    bsdf.inputs["Roughness"].default_value = 0.45
    mat.blend_method = "OPAQUE"
    # The cards are single-sided ribbons seen from both faces, so culling them
    # would punch holes in the hair from half the angles the camera reaches.
    mat.use_backface_culling = False
    return cut


def join_and_bind_head(arm):
    """Join the loose head and hair into one mesh and rigid-bind it to `Head`.

    Must run AFTER the backup copy is gone. join() acts on the selection and an
    object in a hidden collection cannot be selected, so run first it merges the
    visible half, reports success, and leaves the other 183 loose.

    Reads bpy.data.objects, not the view layer: removing a collection's objects
    leaves view_layer.objects stale -- it still reports the pre-delete count and
    hands back None for every object that is gone.
    """
    bpy.context.view_layer.update()
    loose = [o for o in bpy.data.objects
             if o.type == "MESH" and o.parent is None]
    if not loose:
        raise RuntimeError("no loose head/hair meshes found")

    bpy.ops.object.select_all(action="DESELECT")
    for o in loose:
        o.select_set(True)
    bpy.context.view_layer.objects.active = loose[0]
    if len(loose) > 1:
        bpy.ops.object.join()
    head = bpy.context.view_layer.objects.active
    head.name = "Head_Hair"

    if HEAD_BONE not in arm.data.bones:
        raise RuntimeError("armature has no %r bone to bind the head to"
                           % HEAD_BONE)

    # Rigid: one group, every vertex at 1.0. Not automatic weights -- there is
    # no body underneath this geometry for a heat solve to bleed into, and a
    # skull does not deform.
    for vg in list(head.vertex_groups):
        head.vertex_groups.remove(vg)
    vg = head.vertex_groups.new(name=HEAD_BONE)
    vg.add(range(len(head.data.vertices)), 1.0, "REPLACE")

    world = head.matrix_world.copy()
    head.parent = arm
    head.matrix_parent_inverse = arm.matrix_world.inverted()
    head.matrix_world = world
    if not any(m.type == "ARMATURE" for m in head.modifiers):
        mod = head.modifiers.new(name="Armature", type="ARMATURE")
        mod.object = arm
    return len(loose), len(head.data.vertices)


def strip_color_attributes():
    """Drop colour attributes no material reads.

    This game's skinning shader multiplies COLOR_0 into finalColor, so an
    unused black or zero-alpha attribute renders the model black. Same reason
    tools/retarget_miniboss.py's phase4 does it.
    """
    stripped = []
    for obj in bpy.data.objects:
        if obj.type != "MESH":
            continue
        for attr in list(obj.data.color_attributes):
            stripped.append("%s.%s" % (obj.name, attr.name))
            obj.data.color_attributes.remove(attr)
    return stripped


def main():
    dropped = drop_backup_collection()
    empties = drop_stray_empties()
    arm = the_armature()
    arm.name = "KimonoArmature"          # so it cannot collide with pack.blend's
    sword_meshes, sword_bones = drop_second_sword(arm)
    cut = flatten_hair()
    joined, head_verts = join_and_bind_head(arm)
    stripped = strip_color_attributes()

    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    unparented = [o.name for o in meshes if o.parent is not arm]
    if unparented:
        raise RuntimeError("meshes left unskinned: %s" % unparented)

    print("KIMONO_SOURCE " + repr({
        "dropped_backup_copy": len(dropped),
        "dropped_empties": empties,
        "dropped_sword_meshes": sword_meshes,
        "dropped_sword_bones": sword_bones,
        "hair_links_cut": cut,
        "head_pieces_joined": joined,
        "head_verts": head_verts,
        "color_attrs_stripped": stripped,
        "meshes_out": len(meshes),
        "verts_out": sum(len(o.data.vertices) for o in meshes),
        "bones_out": len(arm.data.bones),
    }))

    bpy.ops.wm.save_as_mainfile(filepath=OUT)
    print("SAVED " + OUT)


if __name__ == "__main__":
    main()
