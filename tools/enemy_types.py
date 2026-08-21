"""The enemy-type names, read from the one place they are declared.

A leaf module on purpose: it imports nothing but the standard library, and
specifically not bpy. That is what lets export_level.py (which cannot be
imported outside Blender, because it imports bpy at module level) and
verify_level.py (which must never touch Blender at all) both read the same
table. The dependency only runs one way, and this is the end of it.

The table itself is include/Entities/EnemyTypes.def, an X-macro file the C++
enum and its name table are both generated from. Parsing it here means a type
added in one place is known everywhere, instead of the three hand-synced lists
this replaced.
"""

import os
import re

DEF_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "..", "include", "Entities", "EnemyTypes.def")

_ROW = re.compile(r"^\s*ENEMY_TYPE\(\s*(\w+)\s*\)", re.MULTILINE)


def enemy_types():
    """Every type name, in declaration order.

    Exits rather than returning an empty list on any failure. An empty table
    would make export_level.py reject every enemy marker in the map with a
    message about types not matching, which is a long way from the real
    problem -- so the real problem gets said out loud instead.
    """
    path = os.path.normpath(DEF_PATH)
    try:
        with open(path) as f:
            source = f.read()
    except OSError as error:
        raise SystemExit("enemy_types: cannot read %s (%s). It is the one "
                         "place enemy types are declared; nothing downstream "
                         "can work without it." % (path, error))

    names = _ROW.findall(source)
    if not names:
        raise SystemExit("enemy_types: %s parsed to zero rows. Every enemy "
                         "marker would be rejected as an unknown type. Check "
                         "that its rows still read ENEMY_TYPE(Name)." % path)
    return names


if __name__ == "__main__":
    print("\n".join(enemy_types()))
