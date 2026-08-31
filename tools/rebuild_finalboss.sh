#!/bin/sh
# Rebuild the final boss asset from the hand-made source file, end to end.
#
#     tools/rebuild_finalboss.sh
#
# The order is not negotiable. build_finalboss_pack.py is a REPLACEMENT: it
# drops every clip in the file and re-imports from the FBX pack, so anything the
# authoring passes made is lost and has to be remade after it. make_..._moves.py
# runs last because it consumes and then DELETES the src_ scratch clips the
# other passes read.
set -e

# --python-exit-code makes Blender FAIL on a script exception. Without it it
# exits 0 and `set -e` sails past: a pass that raised was silently skipped and
# the GLB got exported from a half-built .blend. Found the hard way.

BLENDER=${BLENDER:-/Applications/Blender.app/Contents/MacOS/Blender}
MODELS=${MODELS:-$HOME/Documents/3D/Model}
HERE=$(cd "$(dirname "$0")/.." && pwd)

SRC="$MODELS/finalboss.blend"           # hand-made; never written to
PACK="$MODELS/pack_finalboss.blend"     # generated, authoring scale (0.01)
BIG="$MODELS/pack_finalboss_scaled.blend"  # generated, final size

"$BLENDER" --python-exit-code 1 -b "$SRC"  --python "$HERE/tools/build_finalboss_pack.py" -- --save
"$BLENDER" --python-exit-code 1 -b "$PACK" --python "$HERE/tools/make_finalboss_guard.py" -- --save
"$BLENDER" --python-exit-code 1 -b "$PACK" --python "$HERE/tools/make_finalboss_break.py" -- --save
"$BLENDER" --python-exit-code 1 -b "$PACK" --python "$HERE/tools/make_finalboss_moves.py" -- --save

# Scale LAST. Every pose target in the authoring passes is an absolute
# world-metre coordinate solved against the unscaled rig, so the rig has to stay
# unscaled until they have all run.
"$BLENDER" --python-exit-code 1 -b "$PACK" --python "$HERE/tools/scale_finalboss.py"

"$BLENDER" --python-exit-code 1 --background "$BIG" --python "$HERE/tools/merge_animations.py" \
    -- "$HERE/assets/FinalBoss.glb"
"$BLENDER" --python-exit-code 1 --background --python "$HERE/tools/bake_root_motion.py" \
    -- "$HERE/assets/FinalBoss.glb" "$HERE/assets/FinalBoss.rootmotion.glb"

echo "FinalBoss.rootmotion.glb rebuilt"
