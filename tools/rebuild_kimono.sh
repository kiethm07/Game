#!/bin/sh
# Rebuild the kimono enemy asset from the hand-made source file, end to end.
#
#     tools/rebuild_kimono.sh
#
# The order is not negotiable, and each step reads what the one before it wrote:
#
#   1. make_kimono_source.py  GH10_textured.blend -> kimono.blend
#      Deletes the `Backup` collection -- a complete second copy of the
#      character that being hidden in the view layer does NOT keep out of an
#      export -- joins the 183 loose head and hair pieces into one mesh and
#      rigid-binds them to `Head`, and drops the second katana.
#   2. retarget_kimono.py     pack.blend + kimono.blend -> pack_kimono.blend
#      Moves the skin onto the Mixamo rig. Asserts the mirrored side mapping
#      before it runs and refuses to exceed skinning.vs's MAX_BONE_NUM.
#   3. build_kimono_pack.py   swaps the borrowed player clips for the nodachi
#      set. A REPLACEMENT: it deletes every clip armature except the two it
#      rescues, so anything authored into the file afterwards is lost and would
#      have to be remade after this step, not before.
#   4/5. merge + bake root motion -> the two GLBs the game loads.
set -e

# --python-exit-code makes Blender FAIL on a script exception. Without it it
# exits 0 and `set -e` sails past: a pass that raised is silently skipped and
# the GLB gets exported from a half-built .blend.

BLENDER=${BLENDER:-/Applications/Blender.app/Contents/MacOS/Blender}
MODELS=${MODELS:-$HOME/Documents/3D/Model}
HERE=$(cd "$(dirname "$0")/.." && pwd)

SRC="$MODELS/Kimono_enemy/GH10_textured.blend"  # hand-made; never written to
CLEAN="$MODELS/kimono.blend"                    # generated, one clean character
PACKSRC="$MODELS/pack.blend"                    # the Mixamo rig + clip library
PACK="$MODELS/pack_kimono.blend"                # generated, skin + nodachi clips

"$BLENDER" --python-exit-code 1 -b "$SRC"     --python "$HERE/tools/make_kimono_source.py"
"$BLENDER" --python-exit-code 1 -b "$PACKSRC" --python "$HERE/tools/retarget_kimono.py"  -- --save
"$BLENDER" --python-exit-code 1 -b "$PACK"    --python "$HERE/tools/build_kimono_pack.py" -- --save

"$BLENDER" --python-exit-code 1 --background "$PACK" --python "$HERE/tools/merge_animations.py" \
    -- "$HERE/assets/KimonoEnemy.glb"
"$BLENDER" --python-exit-code 1 --background --python "$HERE/tools/bake_root_motion.py" \
    -- "$HERE/assets/KimonoEnemy.glb" "$HERE/assets/KimonoEnemy.rootmotion.glb"

echo "KimonoEnemy.rootmotion.glb rebuilt"
