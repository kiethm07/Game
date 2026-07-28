"""Bake Mixamo hip-baked locomotion onto a dedicated root bone.

Mixamo exports with "In Place" unchecked put the character's travel on
`mixamorig:Hips`, mixed together with vertical bob and lateral weight shift.
That makes the two impossible to separate at runtime -- the engine cannot tell
a stride from a sway without a tuned magnitude threshold.

This script inserts a real root bone and moves the horizontal travel onto it:

    Root                 origin, on the floor, horizontal locomotion only
      +-- mixamorig:Hips bob and sway only; no net horizontal travel

Conventions the runtime relies on:
  * `Root` is bone index 0 of the exported skin, at the origin on frame 0.
  * Only HORIZONTAL translation is baked. Vertical stays on the hips so the
    engine keeps authority over jump arcs via its own gravity integration.
  * Only the component ALONG the clip's net travel direction is baked. Lateral
    sway stays on the hips -- baking it would put a wobble on the root, and the
    runtime cancels the root at draw time, which would drag the whole mesh.
    In-place clips (no net travel) leave the root completely static.
  * Clip order and names are preserved, so animation indices do not shift.

Usage:
    blender --background --python tools/bake_root_motion.py -- <in.glb> <out.glb>

Axis note: glTF is Y-up, but Blender's importer converts to Z-up. Inside this
script "horizontal" therefore means X/Y and "vertical" means Z. The exporter
converts back on the way out.
"""

import sys
import os

import bpy
from mathutils import Vector

ROOT_BONE = "Root"
HIPS_BONE = "mixamorig:Hips"

# raylib's glTF loader resamples every clip to 60 Hz (GLTF_FRAMERATE in
# rmodels.c). Matching it here keeps the bake lossless: at Blender's default 24
# fps the frame range rounds off the tail of short clips, which measurably
# shortened Run's stride.
FPS = 60

# Below this much net horizontal travel a clip is in-place (an idle's weight
# shift, a turn). Nothing is baked onto the root for those.
MIN_TRAVEL = 1e-4


def parse_args():
    argv = sys.argv
    if "--" not in argv:
        raise SystemExit("usage: blender --background --python bake_root_motion.py -- <in.glb> <out.glb>")
    args = argv[argv.index("--") + 1:]
    if len(args) != 2:
        raise SystemExit("expected exactly 2 arguments: <in.glb> <out.glb>")
    src, dst = args
    if not os.path.isfile(src):
        raise SystemExit("input not found: %s" % src)
    return os.path.abspath(src), os.path.abspath(dst)


def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    # Must be set before import: the glTF importer converts keyframe times to
    # frame numbers using the scene fps.
    bpy.context.scene.render.fps = FPS


def find_armature():
    for obj in bpy.data.objects:
        if obj.type == "ARMATURE":
            return obj
    raise SystemExit("no armature found in the imported file")


def add_root_bone(arm_obj):
    """Insert ROOT_BONE at the armature origin and reparent the hips to it."""
    bpy.context.view_layer.objects.active = arm_obj
    bpy.ops.object.mode_set(mode="EDIT")

    edit_bones = arm_obj.data.edit_bones
    if ROOT_BONE in edit_bones:
        raise SystemExit("armature already has a '%s' bone -- already baked?" % ROOT_BONE)
    if HIPS_BONE not in edit_bones:
        raise SystemExit("expected a '%s' bone; found: %s"
                         % (HIPS_BONE, sorted(b.name for b in edit_bones)[:8]))

    root = edit_bones.new(ROOT_BONE)
    root.head = Vector((0.0, 0.0, 0.0))
    root.tail = Vector((0.0, 0.2, 0.0))

    hips = edit_bones[HIPS_BONE]
    hips.parent = root
    # Keep the hips where they are; the root is a pure motion carrier.
    hips.use_connect = False

    bpy.ops.object.mode_set(mode="OBJECT")


def sample_hips(arm_obj, action, frames):
    """Pass 1 -- read the hips' full armature-space matrix for every frame.

    Must complete before any keyframe is written: once Root starts moving, the
    hips' armature-space matrix reflects the new parent and the samples become
    self-referential.

    The whole matrix is kept, not just the translation, because pass 2 restores
    the pose by assigning it back -- see write_tracks.
    """
    arm_obj.animation_data.action = action

    # Park the root at the origin first. The action being sampled has no root
    # keyframes yet, so the pose bone would otherwise still hold wherever the
    # PREVIOUS action's bake left it -- silently offsetting every sample, and
    # with it the entire baked skeleton, by that action's stride.
    reset_root(arm_obj)

    hips = arm_obj.pose.bones[HIPS_BONE]

    samples = []
    for f in frames:
        bpy.context.scene.frame_set(f)
        # frame_set re-evaluates the action, which cannot touch the root (no
        # keyframes for it), but can leave the root wherever it was; re-park it
        # so the sampled hips matrices are relative to a root at the origin.
        reset_root(arm_obj)
        samples.append(hips.matrix.copy())
    return samples


def reset_root(arm_obj):
    """Park the root pose bone at the origin and re-evaluate."""
    arm_obj.pose.bones[ROOT_BONE].location = Vector((0.0, 0.0, 0.0))
    bpy.context.view_layer.update()


def rotation_path(pose_bone):
    """The rotation data path matching the bone's rotation mode."""
    mode = pose_bone.rotation_mode
    if mode == "QUATERNION":
        return "rotation_quaternion"
    if mode == "AXIS_ANGLE":
        return "rotation_axis_angle"
    return "rotation_euler"


def travel_axis(samples):
    """Unit vector of the clip's net horizontal travel, or None if in-place."""
    start = samples[0].to_translation()
    end = samples[-1].to_translation()
    net = Vector((end.x - start.x, end.y - start.y, 0.0))
    if net.length < MIN_TRAVEL:
        return None
    return net.normalized()


def write_tracks(arm_obj, action, frames, samples):
    """Pass 2 -- move horizontal travel from the hips onto the root.

    The hips are restored by assigning back the armature-space matrix sampled in
    pass 1, rather than by computing a location offset by hand. Pose-bone
    `location` is expressed along the bone's own rest axes and relative to its
    parent, so with Mixamo's rotated hips a hand-computed offset silently
    displaces the whole character. Assigning `.matrix` makes Blender do that
    conversion, and it stays correct whatever the new parent is doing.

    Every channel is keyed, not just location. Giving the hips a parent changes
    the basis its local transform is expressed in, which invalidates the
    rotation curves the clip arrived with -- left alone they re-interpret as a
    different pose and the vertical bob comes out along a horizontal axis. This
    is the same thing Blender's own "Bake Action (visual keying)" does.
    """
    root = arm_obj.pose.bones[ROOT_BONE]
    hips = arm_obj.pose.bones[HIPS_BONE]

    origin = samples[0].to_translation()
    axis = travel_axis(samples)

    for f, sample in zip(frames, samples):
        bpy.context.scene.frame_set(f)

        # Only the component along the travel axis goes on the root. Projecting
        # keeps the authored acceleration profile -- which is what makes a lunge
        # or a dodge read correctly -- while leaving lateral sway on the hips.
        if axis is None:
            travel = Vector((0.0, 0.0, 0.0))
        else:
            position = sample.to_translation()
            offset = Vector((position.x - origin.x, position.y - origin.y, 0.0))
            travel = axis * offset.dot(axis)

        root.location = travel
        root.keyframe_insert(data_path="location", frame=f)

        # Re-evaluate so the hips see the root's new position as their parent.
        bpy.context.view_layer.update()

        # Put the hips back exactly where they were in armature space. The root
        # now supplies the travel, so what remains on the hips is bob and sway.
        hips.matrix = sample
        for path in ("location", rotation_path(hips), "scale"):
            hips.keyframe_insert(data_path=path, frame=f)


def bake_action(arm_obj, action):
    start, end = (int(round(v)) for v in action.frame_range)
    frames = list(range(start, end + 1))
    if len(frames) < 2:
        print("  %-28s skipped (%d frame)" % (action.name, len(frames)))
        return

    samples = sample_hips(arm_obj, action, frames)
    write_tracks(arm_obj, action, frames, samples)

    travel = samples[-1].to_translation() - samples[0].to_translation()
    net = Vector((travel.x, travel.y, 0.0)).length
    kind = "in-place" if travel_axis(samples) is None else "root motion"
    print("  %-28s frames=%-5d net horizontal=%.3f  (%s)"
          % (action.name, len(frames), net, kind))


def main():
    src, dst = parse_args()

    reset_scene()
    bpy.ops.import_scene.gltf(filepath=src)

    arm_obj = find_armature()
    add_root_bone(arm_obj)

    if arm_obj.animation_data is None:
        arm_obj.animation_data_create()

    actions = list(bpy.data.actions)
    print("Baking %d action(s) in %s" % (len(actions), os.path.basename(src)))
    for action in actions:
        bake_action(arm_obj, action)

    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.export_scene.gltf(
        filepath=dst,
        export_format="GLB",
        export_animations=True,
        # Root carries no vertex weights. Exporting deform bones only would drop
        # it from skin.joints and leave the hips as joint 0 -- the whole point of
        # this script would be silently undone.
        export_def_bones=False,
        export_yup=True,
        export_frame_step=1,
        export_anim_slide_to_zero=False,
        export_optimize_animation_size=False,
    )
    print("Wrote %s" % dst)


if __name__ == "__main__":
    main()
