"""Build the campaign's two interior/arena phases as playable greyboxes.

Phases 1 and 2 are cut from the kitbashed castle-approach art by
tools/make_castle_level.py. Phases 3 and 4 have no source art at all -- the
kit that map was built from is entirely exterior (walls, towers, a keep shell,
rocks, trees), and you cannot kitbash a room out of the outside of a building.
So they are built from primitives here, the same way make_greybox_level.py
builds its arena, and art-passed in Blender later.

    Phase 3  castle interior -- entry hall, pillared great hall with raised
             galleries, rear corridor, out the north door.
    Phase 4  battlefield     -- open arena around a central dais, ruined walls
             and boulders for cover. The final boss.

Both are authored in GAME space (Y-up) and converted on the way into Blender,
because every dimension here is a gameplay dimension -- corridor widths against
the player's 1.8 m, ceiling heights, how far a ramp climbs -- and writing them
in the space they will be played in is what makes them checkable.

Every BOX_/RAMP_ proxy gets a matching VIS_ solid, which is worth more here than
it was for the kitbash: tools/verify_level.py can only do its per-proxy
alignment check when the visual meshes are named after the proxies, and a
kitbashed map has no such pairing. These two levels get the strong check.

Usage:
    blender --background --python tools/make_campaign_greybox.py -- \
        --phase 3 [--out source/levels/phase3_interior.blend]

Then export and verify, exactly as for any other level:
    blender --background source/levels/phase3_interior.blend \
        --python tools/export_level.py -- --out-dir assets/levels/phase3_interior
    python3 tools/verify_level.py assets/levels/phase3_interior

Re-running is safe: the scene is reset to empty and rebuilt from the tables
below, so nothing accumulates and there is no authored art to damage.
"""

import argparse
import math
import os
import sys

import bpy

# Blender does not put a --python script's own directory on sys.path.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from make_greybox_level import (       # noqa: E402
    BOX_FACES, add_mesh, box_verts, make_collection, make_material,
    ramp_corners_game, reset_scene, to_blender,
)

# --- Palette ----------------------------------------------------------------
# Role-coded so the in-game debug overlay is readable, matching the convention
# make_castle_level.py uses for its proxies.

STONE = (128, 124, 116)      # structural walls
FLOOR = (96, 88, 78)         # floors and ground
TIMBER = (122, 82, 48)       # pillars, partitions, galleries
DAIS = (146, 118, 74)        # raised platforms
RAMPC = (168, 150, 104)      # ramps
RUIN = (108, 104, 100)       # broken cover
ROCK = (92, 94, 98)          # boulders

# --- Shared dimensions ------------------------------------------------------
# Named rather than repeated, because these are the numbers that get tuned
# after the first playtest and they need to move together.

WALL_H = 8.0                 # outer wall height, interior
WALL_T = 1.5                 # outer wall thickness
PART_H = 7.0                 # interior partition height
DOOR_HALF = 3.0              # half-width of a doorway gap
GALLERY_Y = 4.0              # raised gallery walking height

# ---------------------------------------------------------------------------
# Phase 3 -- the castle interior
# ---------------------------------------------------------------------------
#
# A 60 x 60 m keep interior, entered from the south and left by the north door.
# Laid out as three rooms in a line so the phase reads as a route rather than an
# arena: entry hall, great hall, rear corridor.
#
#   z = +30  south wall, doorway at x in [-3, 3]   <- arrive from phase 2
#   z = +12  partition, doorway
#            GREAT HALL: four pillars, galleries up both long walls
#   z = -12  partition, doorway
#            REAR CORRIDOR with a side chamber either side
#   z = -30  north wall, doorway                   -> depart to phase 4
#
# Ramp note: PhysicsObstacle::getHeightAt interpolates a ramp along its *longer*
# footprint axis, so every ramp below is deliberately longer along the axis it
# slopes down. export_level.py rejects the other case.

PHASE3_BOXES = [
    ("BOX_Floor", (-30.0, -2.0, -30.0), (30.0, 0.0, 30.0), FLOOR, 0.0),

    # Outer shell. South and north walls are split either side of a doorway.
    ("BOX_WallSouthW", (-30.0, 0.0, 30.0 - WALL_T), (-DOOR_HALF, WALL_H, 30.0),
     STONE, 0.0),
    ("BOX_WallSouthE", (DOOR_HALF, 0.0, 30.0 - WALL_T), (30.0, WALL_H, 30.0),
     STONE, 0.0),
    ("BOX_WallNorthW", (-30.0, 0.0, -30.0), (-DOOR_HALF, WALL_H, -30.0 + WALL_T),
     STONE, 0.0),
    ("BOX_WallNorthE", (DOOR_HALF, 0.0, -30.0), (30.0, WALL_H, -30.0 + WALL_T),
     STONE, 0.0),
    ("BOX_WallEast", (30.0 - WALL_T, 0.0, -30.0), (30.0, WALL_H, 30.0),
     STONE, 0.0),
    ("BOX_WallWest", (-30.0, 0.0, -30.0), (-30.0 + WALL_T, WALL_H, 30.0),
     STONE, 0.0),

    # Entry hall | great hall.
    ("BOX_PartSouthW", (-28.5, 0.0, 11.0), (-4.0, PART_H, 12.5), TIMBER, 0.0),
    ("BOX_PartSouthE", (4.0, 0.0, 11.0), (28.5, PART_H, 12.5), TIMBER, 0.0),

    # Great hall | rear corridor.
    ("BOX_PartNorthW", (-28.5, 0.0, -12.5), (-4.0, PART_H, -11.0), TIMBER, 0.0),
    ("BOX_PartNorthE", (4.0, 0.0, -12.5), (28.5, PART_H, -11.0), TIMBER, 0.0),

    # Great hall pillars.
    ("BOX_PillarNW", (-11.0, 0.0, -6.0), (-9.0, PART_H, -4.0), TIMBER, 0.0),
    ("BOX_PillarNE", (9.0, 0.0, -6.0), (11.0, PART_H, -4.0), TIMBER, 0.0),
    ("BOX_PillarSW", (-11.0, 0.0, 4.0), (-9.0, PART_H, 6.0), TIMBER, 0.0),
    ("BOX_PillarSE", (9.0, 0.0, 4.0), (11.0, PART_H, 6.0), TIMBER, 0.0),

    # Raised galleries down the great hall's long walls. Solid terraces rather
    # than floating walkways: a greybox should not need the player to work out
    # what is holding a platform up.
    ("BOX_GalleryWest", (-28.5, 0.0, -11.0), (-22.0, GALLERY_Y, 0.0),
     TIMBER, 0.0),
    ("BOX_GalleryEast", (22.0, 0.0, -11.0), (28.5, GALLERY_Y, 0.0),
     TIMBER, 0.0),

    # The lord's dais at the head of the great hall.
    ("BOX_Dais", (-8.0, 0.0, -11.0), (8.0, 1.5, -6.0), DAIS, 0.0),

    # Rear corridor dividers, each split to leave a doorway into its chamber.
    ("BOX_RearDivWestN", (-12.5, 0.0, -28.5), (-11.0, PART_H, -20.0),
     TIMBER, 0.0),
    ("BOX_RearDivWestS", (-12.5, 0.0, -16.0), (-11.0, PART_H, -12.5),
     TIMBER, 0.0),
    ("BOX_RearDivEastN", (11.0, 0.0, -28.5), (12.5, PART_H, -20.0),
     TIMBER, 0.0),
    ("BOX_RearDivEastS", (11.0, 0.0, -16.0), (12.5, PART_H, -12.5),
     TIMBER, 0.0),
]

PHASE3_RAMPS = [
    # Up onto the galleries. Footprint is 6.5 x 10, so the slope runs along z:
    # high at z = 0 where it meets the gallery, down to the floor at z = 10.
    #
    # Named "Stair", not "GalleryWest", and that matters: the VIS_ solid is
    # named by stripping the BOX_/RAMP_ prefix, so a RAMP_GalleryWest next to a
    # BOX_GalleryWest would ask for two meshes called VIS_GalleryWest. Blender
    # would quietly rename the second, and verify_level.py would then compare
    # the ramp against the gallery's mesh and report a 11 m mismatch that is
    # really a naming collision. check_vis_names() below enforces this.
    ("RAMP_StairWest", (-28.5, 0.0), (-22.0, 10.0), GALLERY_Y, 0.0, RAMPC),
    ("RAMP_StairEast", (22.0, 0.0), (28.5, 10.0), GALLERY_Y, 0.0, RAMPC),
    # Up onto the dais: 8 wide, 10 long, so it slopes along z as well.
    ("RAMP_DaisStep", (-4.0, -6.0), (4.0, 4.0), 1.5, 0.0, RAMPC),
]

# (x, y, z) in game space. y is the surface the enemy stands on.
PHASE3_ENEMIES = [
    (-8.0, 0.0, 20.0),          # entry hall, flanking the door
    (8.0, 0.0, 20.0),
    (-6.0, 0.0, 2.0),           # great hall floor
    (6.0, 0.0, 2.0),
    (-25.0, GALLERY_Y, -5.0),   # west gallery, above the hall
    (0.0, 0.0, -22.0),          # rear corridor, guarding the north door
]

# Facing yaw 0 looks down game -z, which is the direction of travel through
# every phase: in from the south door, out through the north one.
PHASE3_SPAWN = ((0.0, 0.0, 27.0), 0.0)

# ---------------------------------------------------------------------------
# Phase 4 -- the battlefield
# ---------------------------------------------------------------------------
#
# 120 x 120 m of open ground around a central dais, walled all round: this is
# the last phase, so it has an entrance and no exit. Cover is deliberately
# sparse and off-centre -- the fight wants sightlines, and the ruins are there
# to break a straight retreat rather than to hide behind.

PHASE4_BOXES = [
    ("BOX_Ground", (-60.0, -2.0, -60.0), (60.0, 0.0, 60.0), FLOOR, 0.0),

    # Rim. Taller than the interior walls so the arena reads as enclosed
    # ground rather than a room.
    ("BOX_RimSouthW", (-60.0, 0.0, 58.0), (-DOOR_HALF, 12.0, 60.0), STONE, 0.0),
    ("BOX_RimSouthE", (DOOR_HALF, 0.0, 58.0), (60.0, 12.0, 60.0), STONE, 0.0),
    ("BOX_RimNorth", (-60.0, 0.0, -60.0), (60.0, 12.0, -58.0), STONE, 0.0),
    ("BOX_RimEast", (58.0, 0.0, -60.0), (60.0, 12.0, 60.0), STONE, 0.0),
    ("BOX_RimWest", (-60.0, 0.0, -60.0), (-58.0, 12.0, 60.0), STONE, 0.0),

    # The dais the boss holds.
    ("BOX_Dais", (-20.0, 0.0, -20.0), (20.0, 2.0, 20.0), DAIS, 0.0),

    # Ruined walls, yawed off-axis so the arena is not a grid -- and so the
    # export has proxies whose rotation sign actually matters.
    ("BOX_RuinWest", (-40.0, 0.0, 10.0), (-28.0, 5.0, 11.5), RUIN, 20.0),
    ("BOX_RuinEast", (26.0, 0.0, -14.0), (40.0, 4.0, -12.5), RUIN, -35.0),
    ("BOX_RuinNorth", (-14.0, 0.0, -42.0), (-2.0, 4.5, -40.5), RUIN, 60.0),
    ("BOX_RuinSouth", (8.0, 0.0, 36.0), (22.0, 3.5, 37.5), RUIN, -15.0),

    # Boulders.
    ("BOX_RockA", (-46.0, 0.0, -30.0), (-40.0, 3.0, -24.0), ROCK, 15.0),
    ("BOX_RockB", (34.0, 0.0, 30.0), (41.0, 3.5, 37.0), ROCK, -25.0),
    ("BOX_RockC", (-36.0, 0.0, 44.0), (-30.0, 2.5, 50.0), ROCK, 40.0),
    ("BOX_RockD", (44.0, 0.0, -46.0), (50.0, 3.0, -40.0), ROCK, 10.0),
]

PHASE4_RAMPS = [
    # Four approaches onto the dais. The north/south pair is 10 x 14 so it
    # slopes along z; the east/west pair is 14 x 10 so it slopes along x.
    ("RAMP_DaisSouth", (-5.0, 20.0), (5.0, 34.0), 2.0, 0.0, RAMPC),
    ("RAMP_DaisNorth", (-5.0, -34.0), (5.0, -20.0), 0.0, 2.0, RAMPC),
    ("RAMP_DaisEast", (20.0, -5.0), (34.0, 5.0), 2.0, 0.0, RAMPC),
    ("RAMP_DaisWest", (-34.0, -5.0), (-20.0, 5.0), 0.0, 2.0, RAMPC),
]

PHASE4_ENEMIES = [
    (0.0, 2.0, 0.0),            # the boss, centre of the dais
    (-14.0, 0.0, 30.0),         # adds, between the door and the dais
    (14.0, 0.0, 30.0),
    (-30.0, 0.0, -20.0),
    (30.0, 0.0, -24.0),
]

PHASE4_SPAWN = ((0.0, 0.0, 52.0), 0.0)

PHASES = {
    "3": {
        "name": "phase3_interior",
        "boxes": PHASE3_BOXES,
        "ramps": PHASE3_RAMPS,
        "enemies": PHASE3_ENEMIES,
        "spawn": PHASE3_SPAWN,
    },
    "4": {
        "name": "phase4_battlefield",
        "boxes": PHASE4_BOXES,
        "ramps": PHASE4_RAMPS,
        "enemies": PHASE4_ENEMIES,
        "spawn": PHASE4_SPAWN,
    },
}


def check_ramp_axis(ramps):
    """Fail early on a ramp that slopes across itself.

    export_level.py rejects these too, but it rejects them after a full scene
    build; catching it off the table costs nothing and points at the line that
    is wrong.
    """
    for name, min_xz, max_xz, start_y, end_y, _ in ramps:
        span_x = abs(max_xz[0] - min_xz[0])
        span_z = abs(max_xz[1] - min_xz[1])
        if start_y == end_y:
            raise SystemExit("%s is flat -- make it a BOX_" % name)
        if span_x == span_z:
            raise SystemExit(
                "%s has a square footprint (%.1f x %.1f), so which axis it "
                "slopes along is a tie broken by >=. Make one axis longer."
                % (name, span_x, span_z))


def check_vis_names(boxes, ramps):
    """No two proxies may want the same VIS_ mesh name.

    The visual solid for a proxy is named by swapping its BOX_/RAMP_ prefix for
    VIS_, so BOX_Foo and RAMP_Foo collide. Blender resolves that by renaming one
    to VIS_Foo.001 without complaint, and the damage lands somewhere else
    entirely: verify_level.py pairs proxies to meshes by name, so it ends up
    measuring one proxy against the other's mesh and reporting a large delta
    that looks like a broken axis conversion.
    """
    seen = {}
    for name in ([b[0] for b in boxes] + [r[0] for r in ramps]):
        vis = name.replace("BOX_", "VIS_").replace("RAMP_", "VIS_")
        if vis in seen:
            raise SystemExit(
                "%s and %s both map to %s. Rename one -- Blender would resolve "
                "this silently and verify_level.py would then compare the wrong "
                "pair." % (seen[vis], name, vis))
        seen[vis] = name


def build(phase):
    spec = PHASES[phase]
    check_ramp_axis(spec["ramps"])
    check_vis_names(spec["boxes"], spec["ramps"])

    reset_scene()
    visual = make_collection("VISUAL")
    collision = make_collection("COLLISION")
    markers = make_collection("MARKERS")

    # Proxies are for authoring, not rendering. Hiding the collection from the
    # render also keeps it out of the glTF export.
    collision.hide_render = True
    collision.hide_viewport = False
    markers.hide_render = True

    for name, min_corner, max_corner, rgb, yaw in spec["boxes"]:
        centre_g = tuple((a + b) * 0.5 for a, b in zip(min_corner, max_corner))
        half_g = tuple((b - a) * 0.5 for a, b in zip(min_corner, max_corner))
        half_b = (half_g[0], half_g[2], half_g[1])
        yaw_rad = math.radians(yaw)

        proxy = add_mesh(collision, name, box_verts(half_b), BOX_FACES, rgb)
        proxy.location = to_blender(centre_g)
        proxy.rotation_euler = (0.0, 0.0, yaw_rad)
        proxy.display_type = "WIRE"

        solid = add_mesh(visual, name.replace("BOX_", "VIS_"),
                         box_verts(half_b), BOX_FACES, rgb,
                         make_material("MAT_" + name, rgb))
        solid.location = proxy.location
        solid.rotation_euler = proxy.rotation_euler

    for name, min_xz, max_xz, start_y, end_y, rgb in spec["ramps"]:
        corners_g = ramp_corners_game(min_xz, max_xz, start_y, end_y)
        corners_b = [to_blender(c) for c in corners_g]

        proxy = add_mesh(collision, name, corners_b, [(0, 1, 2, 3)], rgb)
        proxy.display_type = "WIRE"

        floor_y = min(start_y, end_y)
        bottom_b = [to_blender((c[0], floor_y, c[2])) for c in corners_g]
        add_mesh(visual, name.replace("RAMP_", "VIS_"),
                 corners_b + bottom_b,
                 [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
                  (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)],
                 rgb, make_material("MAT_" + name, rgb))

    position, yaw = spec["spawn"]
    spawn = bpy.data.objects.new("PLAYER_SPAWN", None)
    spawn.empty_display_type = "SINGLE_ARROW"
    spawn.empty_display_size = 1.8
    spawn.location = to_blender(position)
    spawn.rotation_euler = (0.0, 0.0, math.radians(yaw))
    markers.objects.link(spawn)

    # Every enemy is a Swordman because that is the only member of EnemyType --
    # export_level.py validates the name against it and refuses anything else.
    # Phase 4's first marker is the boss by position, not by type; giving it its
    # own type is engine work, not level work.
    for index, position in enumerate(spec["enemies"], start=1):
        marker = bpy.data.objects.new("ENEMY_Swordman_%02d" % index, None)
        marker.empty_display_type = "PLAIN_AXES"
        marker.empty_display_size = 1.0
        marker.location = to_blender(position)
        markers.objects.link(marker)

    return spec


def parse_args(argv):
    argv = argv[argv.index("--") + 1:] if "--" in argv else []
    parser = argparse.ArgumentParser(prog="make_campaign_greybox.py")
    parser.add_argument("--phase", required=True, choices=sorted(PHASES),
                        help="3 = castle interior, 4 = battlefield")
    parser.add_argument("--out", default=None,
                        help="defaults to source/levels/<phase name>.blend")
    return parser.parse_args(argv)


def main():
    args = parse_args(sys.argv)
    spec = build(args.phase)

    out = os.path.abspath(
        args.out or os.path.join("source", "levels", spec["name"] + ".blend"))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=out)

    print("[make_campaign_greybox] phase %s -> %s" % (args.phase, spec["name"]))
    print("[make_campaign_greybox]   %d boxes, %d ramps, %d enemy spawns"
          % (len(spec["boxes"]), len(spec["ramps"]), len(spec["enemies"])))
    print("[make_campaign_greybox]   wrote %s" % out)


if __name__ == "__main__":
    main()
