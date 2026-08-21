#!/usr/bin/env bash
#
# Re-export a phase from its .blend and put it back in a shippable state.
#
#   tools/rebuild_level.sh phase1_forest
#   tools/rebuild_level.sh all
#
# Three steps, and the middle one is the reason this script exists rather than
# a line in a README:
#
#   1. export_level.py    .blend -> level.json + level.glb + collision.bin
#                                   (+ detail.glb where the level has a DETAIL
#                                   collection, which today is phase1's grass)
#   2. prune_tree_proxies.py        drop trunk collision boxes nothing can
#                                   reach or see past
#   3. verify_level.py              six checks against the exported result
#
# Step 2 is not optional after step 1. export_level.py rebuilds `obstacles`
# from the .blend every time, which restores every pruned trunk -- 200 of them
# in phase1 -- and nothing about the level looks wrong afterwards. It just
# quietly costs a per-character obstacle test every frame and 2,400 extra
# triangles in every navmesh bake. Running the two together is the only way
# that does not depend on remembering.
#
# What this deliberately does NOT touch:
#   * enemies.json  -- the exporter never writes it, which is the whole point
#                      of it being an overlay. Hand-authored spawns survive.
#   * assets/props  -- built from the campfire .fbx by tools/make_campfire.py,
#                      not from any level.
#
# If the terrain itself moved (not just collision boxes), phase1's grass was
# scattered against the old surface and should be regrown first:
#     blender --background source/levels/phase1_forest.blend \
#         --python tools/scatter_grass.py
set -euo pipefail

BLENDER="${BLENDER:-/Applications/Blender.app/Contents/MacOS/Blender}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PHASES=(phase1_forest phase2_approach phase3_battlefield)

if [ $# -lt 1 ]; then
    echo "usage: tools/rebuild_level.sh <phase|all>"
    echo "       phases: ${PHASES[*]}"
    exit 2
fi

if [ ! -x "$BLENDER" ]; then
    echo "Blender not found at $BLENDER" >&2
    echo "Set BLENDER=/path/to/Blender and re-run." >&2
    exit 1
fi

if [ "$1" = "all" ]; then
    targets=("${PHASES[@]}")
else
    targets=("$@")
fi

for phase in "${targets[@]}"; do
    blend="$ROOT/source/levels/$phase.blend"
    out="$ROOT/assets/levels/$phase"

    if [ ! -f "$blend" ]; then
        echo "no such .blend: $blend" >&2
        exit 1
    fi

    echo
    echo "=============================================================="
    echo " $phase"
    echo "=============================================================="

    echo "--- 1/3 export ---"
    # Blender swallows tracebacks in --background, so export_level.py turns its
    # own authoring errors into exit 1. `set -e` stops the whole run there
    # rather than pruning and verifying a level that never got written.
    "$BLENDER" --background "$blend" --python "$ROOT/tools/export_level.py" -- \
        --out-dir "$out" --name "$phase" 2>&1 \
        | grep -E "^\[export_level\]|ExportError|Error:" || true

    if [ ! -f "$out/level.json" ]; then
        echo "export produced no level.json -- stopping." >&2
        exit 1
    fi

    echo "--- 2/3 prune tree proxies ---"
    python3 "$ROOT/tools/prune_tree_proxies.py" "$out"

    echo "--- 3/3 verify ---"
    python3 "$ROOT/tools/verify_level.py" "$out"
done

echo
echo "Done. Rebuild the game to pick the levels up:  cmake --build build -j8"
