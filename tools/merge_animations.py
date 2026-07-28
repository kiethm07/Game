"""Merge a .blend full of per-clip Mixamo armatures into a single skinned GLB.

Mixamo hands out one FBX per animation, so importing 51 clips leaves 51
armatures in the file -- `Armature` carries the meshes, `Armature.001` ..
`Armature.NNN` carry nothing but an action each. raylib cannot consume that:
`LoadModelAnimationsGLTF` (rmodels.c) bails unless the file has a skin, and it
matches animation bones to mesh bones BY INDEX, so the clips and the mesh have
to come out of one shared skin. This script folds them together.

Conventions the runtime relies on:
  * One skin, one armature node, all clips indexing the same joint order.
  * The exported armature node is IDENTITY -- scale 1, no rotation. Mixamo
    imports sit under a 0.01 scale and a 90 deg X rotation; both are baked into
    the mesh and rest-bone data here so raylib gets metres.
  * Clip order follows the source armature names sorted numerically, and each
    glTF animation is named via CLIP_NAMES. Resolve clips by name, not by
    hardcoded index -- see Swordman.cpp.
  * Bone-space translation keys are rescaled by the same factor as the baked
    object scale (see `_bake_transforms`), otherwise every clip translates
    100x too far.

Usage:
    blender --background <file.blend> --python tools/merge_animations.py -- <out.glb>
"""

import json
import sys

import bpy
from mathutils import Matrix

MAIN_ARMATURE = "Armature"

# Mixamo names every downloaded action "mixamo.com", so importing a pack leaves
# 51 indistinguishable `Armature.0NN` objects. This table restores the source
# clip names, recovered by matching each action's keyframe fingerprint against a
# fresh import of the FBX pack in `Pro Sword and Shield Pack`. Names are capped
# at 31 characters -- raylib's ModelAnimation.name is char[32].
CLIP_NAMES = {
    "Armature.001": "Idle_3",           "Armature.002": "Idle_2",
    "Armature.003": "Slash_2",          "Armature.004": "Death_2",
    "Armature.005": "Idle",             "Armature.006": "Slash_4",
    "Armature.007": "Attack",           "Armature.008": "Casting",
    "Armature.009": "Idle_4",           "Armature.010": "PowerUp",
    "Armature.011": "Death",            "Armature.012": "Attack_3",
    "Armature.013": "DrawSword",        "Armature.014": "CrouchIdle",
    "Armature.015": "CrouchBlock",      "Armature.016": "Slash_3",
    "Armature.017": "Slash",            "Armature.018": "Attack_2",
    "Armature.019": "Crouching",        "Armature.020": "Kick",
    "Armature.021": "BlockIdle",        "Armature.022": "SheathSword",
    "Armature.023": "Slash_5",          "Armature.024": "CrouchBlockIdle",
    "Armature.025": "Walk",             "Armature.026": "Strafe_2",
    "Armature.027": "Walk_2",           "Armature.028": "Jump_2",
    "Armature.029": "Strafe",           "Armature.030": "Casting_2",
    "Armature.031": "Attack_4",         "Armature.032": "SheathSword_2",
    "Armature.033": "Impact",           "Armature.034": "Impact_2",
    "Armature.035": "Turn_2",           "Armature.036": "Turn",
    "Armature.037": "DrawSword_2",      "Armature.038": "Crouching_2",
    "Armature.039": "Strafe_3",         "Armature.040": "Turn180_2",
    "Armature.041": "Jump",             "Armature.042": "Turn180",
    "Armature.043": "CrouchBlock_2",    "Armature.044": "Impact_3",
    "Armature.045": "Run",              "Armature.046": "Strafe_4",
    "Armature.047": "Block_2",          "Armature.048": "Crouching_3",
    "Armature.049": "Crouch",           "Armature.050": "Block",
    "Armature.051": "Run_2",
}


def _clip_name(armature_name):
    name = CLIP_NAMES.get(armature_name, armature_name)
    if len(name) > 31:
        raise RuntimeError("clip name %r exceeds raylib's 31-char limit" % name)
    return name


def _collect_clips(main):
    """Every other armature in the file, in numeric name order, with its action."""
    others = sorted(
        (o for o in bpy.data.objects if o.type == "ARMATURE" and o.name != main.name),
        key=lambda o: o.name,
    )
    clips = []
    for obj in others:
        anim = obj.animation_data
        if not (anim and anim.action):
            raise RuntimeError("%s holds no action" % obj.name)
        # The action has to outlive the object we are about to delete.
        anim.action.use_fake_user = True
        clips.append((obj.name, anim.action, anim.action_slot))
    return clips


def _strip_scene(main):
    """Drop everything that is not the skinned character."""
    meshes = [o for o in bpy.data.objects if o.type == "MESH" and o.parent is main]
    if not meshes:
        raise RuntimeError("no meshes parented to %s" % main.name)
    main.hide_viewport = False
    main.hide_render = False

    keep = {main.name} | {m.name for m in meshes}
    for obj in list(bpy.data.objects):
        if obj.name not in keep:
            bpy.data.objects.remove(obj, do_unlink=True)
    return meshes


def _bake_transforms(main, meshes):
    """Fold each object's world matrix into its data so the nodes export as identity.

    Returns the uniform scale factor that was baked, which the caller needs in
    order to fix up the actions.
    """
    scale = main.matrix_world.to_scale()
    if max(abs(scale.x - scale.y), abs(scale.x - scale.z)) > 1e-9:
        raise RuntimeError("non-uniform object scale %r; keyframe fixup invalid" % (scale,))

    # Snapshot every world matrix BEFORE mutating anything -- clearing the
    # armature's transform would otherwise change what the parented meshes
    # report on the next read.
    world = {obj.name: obj.matrix_world.copy() for obj in [main] + meshes}

    seen = set()
    for obj in [main] + meshes:
        if obj.data.name not in seen:
            obj.data.transform(world[obj.name])
            seen.add(obj.data.name)
        obj.matrix_parent_inverse = Matrix.Identity(4)
        obj.matrix_basis = Matrix.Identity(4)

    bpy.context.view_layer.update()
    return scale.x


def _rescale_translation_keys(factor):
    """Rescale every pose-bone location channel by `factor`.

    Pose-bone translation lives in bone space, whose unit is the armature's
    LOCAL unit. Baking the 0.01 object scale into the rest bones redefines that
    unit from centimetres to metres, so the stored keys have to shrink to
    match. Rotation channels need no fixup: every bone space rotates together,
    so the world result is unchanged.
    """
    count = 0
    for action in bpy.data.actions:
        for layer in action.layers:
            for strip in layer.strips:
                for bag in strip.channelbags:
                    for fcurve in bag.fcurves:
                        if not fcurve.data_path.endswith(".location"):
                            continue
                        for key in fcurve.keyframe_points:
                            key.co.y *= factor
                            key.handle_left.y *= factor
                            key.handle_right.y *= factor
                        fcurve.update()
                        count += 1
    return count


def _build_nla(main, clips):
    """One muted-nothing NLA track per clip; the exporter emits one animation each."""
    if main.animation_data is None:
        main.animation_data_create()
    anim = main.animation_data
    anim.action = None
    for track in list(anim.nla_tracks):
        anim.nla_tracks.remove(track)

    for source, action, slot in clips:
        name = _clip_name(source)
        track = anim.nla_tracks.new()
        track.name = name
        strip = track.strips.new(name, int(action.frame_range[0]), action)
        if slot is not None:
            strip.action_slot = slot
        track.mute = False
        track.is_solo = False


def main():
    out_path = sys.argv[sys.argv.index("--") + 1]
    armature = bpy.data.objects[MAIN_ARMATURE]

    clips = _collect_clips(armature)
    meshes = _strip_scene(armature)
    factor = _bake_transforms(armature, meshes)
    curves = _rescale_translation_keys(factor)
    _build_nla(armature, clips)

    bpy.ops.export_scene.gltf(
        filepath=out_path,
        export_format="GLB",
        export_yup=True,
        export_apply=False,             # never apply the armature modifier
        export_skins=True,
        export_def_bones=False,         # keep the full joint list
        export_rest_position_armature=True,
        export_animations=True,
        export_animation_mode="NLA_TRACKS",
        export_nla_strips=True,
        export_frame_range=False,       # per-clip range, not the scene range
        export_force_sampling=True,
        export_bake_animation=True,
        export_anim_single_armature=True,
        # Mixamo keys constant scale on all 69 bones; dropping those flat
        # channels is worth ~4 MB across 51 clips.
        export_optimize_animation_size=True,
        export_optimize_animation_keep_anim_armature=True,
        export_materials="EXPORT",
        export_image_format="AUTO",
        use_visible=False,
        use_renderable=False,
        use_active_collection=False,
    )

    print("MERGE_RESULT " + json.dumps({
        "clips": len(clips),
        "meshes": [m.name for m in meshes],
        "baked_scale": round(factor, 6),
        "translation_curves_rescaled": curves,
        "order": [_clip_name(name) for name, _, _ in clips],
    }))


main()
