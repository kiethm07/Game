"""Seed a level's enemies.json from the spawns its Blender markers produced.

The migration step, run once per level. After it, that level's enemies are
edited in enemies.json and the ENEMY_* Empties in its .blend stop mattering --
the overlay replaces them wherever it exists.

Reads level.json's `enemySpawns` and writes the same enemies in the overlay's
flat x/z form, dropping the exported `y`. Dropping it is the point rather than
a shortcut: the engine snaps a spawn with no explicit height onto the ground
under it at load, so the file stays readable and stays correct if the terrain
is later re-exported half a metre lower. Add a `y` back by hand for the one
case that needs it -- something meant to be standing on a bridge, or in the air.

Usage:
    python3 tools/seed_enemies.py assets/levels/<name>
"""

import json
import os
import sys


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: python3 tools/seed_enemies.py <level-dir>")

    level_dir = sys.argv[1]
    out_path = os.path.join(level_dir, "enemies.json")
    if os.path.exists(out_path):
        raise SystemExit(
            "%s already exists. Refusing to overwrite it -- it is hand-authored "
            "and this script only knows how to reproduce the Blender markers, "
            "which is almost certainly a step backwards from whatever is in "
            "there now." % out_path)

    with open(os.path.join(level_dir, "level.json")) as f:
        level = json.load(f)

    spawns = []
    for spawn in level.get("enemySpawns", []):
        position = spawn["position"]
        spawns.append({"type": spawn["type"],
                       "x": round(position[0], 2),
                       "z": round(position[2], 2),
                       "yaw": round(spawn.get("yaw", 0.0), 1)})

    with open(out_path, "w") as f:
        json.dump({"format": 1, "spawns": spawns}, f, indent=2)
        f.write("\n")

    print("[seed_enemies] %s <- %d spawn(s) from level.json"
          % (out_path, len(spawns)))
    print("[seed_enemies] heights dropped; the engine snaps them to the ground "
          "at load. Run tools/verify_level.py to see what it will pick.")


if __name__ == "__main__":
    main()
