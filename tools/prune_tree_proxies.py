"""Drop the tree collision boxes the player can never interact with.

A phase's trees are scattered over the whole terrain, but the player is held
inside a fence and a boundary ring that enclose a fraction of it. Every trunk
proxy outside that enclosure is paid for on every frame -- PhysicsManager walks
the obstacle list per character, Sensor::segmentBlocked walks it per
line-of-sight test -- and rasterised into the Recast heightfield on every level
load, for geometry nothing can ever touch or see past.

Measured on the shipped levels, 267 of 660 trunk proxies are in that category.

WHAT COUNTS AS NEEDED

A proxy is kept if either is true.

  TOUCH   Some reachable ground is within the agent radius plus a margin of it,
          so the player can walk into it.

  SIGHT   It can stand between two reachable points that are within vision range
          of each other, in which case it may be occluding an enemy's view of
          the player and removing it would change what the AI can see.

SIGHT is tested rather than approximated with a keep-radius, and the difference
is worth the code: a blunt 8 m radius keeps 70 trees this test proves are
irrelevant, and a blunt 0 m radius removes 23 that are not. For each of twelve
headings it walks out from the trunk both ways to the vision radius and asks
whether reachable ground lies on BOTH sides. If it does, some sightline crosses
that trunk.

REACHABILITY, AND WHY IT ERRS THE WAY IT DOES

A flood fill from the player spawn over a half-metre grid, where the only things
that block are BOX_Boundary_*, BOX_Fence_* and BOX_Railing_* -- the proxies
authored to stop the player -- each inflated by the navmesh agent radius,
because a gap narrower than the agent is not a gap.

Trees are deliberately treated as PASSABLE during the fill. That is the
conservative direction: a more permissive fill reaches further, calls fewer
trunks unreachable, and therefore under-removes. Treating a thicket as solid
would fence off pockets behind it and start deleting trunks on its far side that
the player can in fact walk around to.

This does NOT reimplement the character controller, and does not try to. It
answers a topological question -- what is enclosed -- using only the geometry
authored to enclose it.

WHY THIS IS NOT PART OF THE EXPORT

tools/export_level.py converts what is authored, and says so loudly when it
cannot. Teaching it to also drop geometry it judged unnecessary would change
what it is. So this runs after it, like verify_level.py does -- and like
verify_level.py it is safe to re-run: a level already pruned reports nothing
left to prune.

The cost of that is a re-export silently restoring the proxies, so
verify_level.py reports how many are prunable. A level that has drifted back
shows up in the check that is already run.

Usage:
    python3 tools/prune_tree_proxies.py assets/levels/<name> [--dry-run]
"""

import json
import math
import os
import sys
from collections import deque

# Grid resolution for the flood fill, in metres. Half the agent radius, so a
# gap the agent could squeeze through cannot fall between two samples.
CELL = 0.5

# NavMeshBuilder's agent radius. Reused rather than re-picked so "can get there"
# means the same thing here as it does to the thing that actually paths.
AGENT_RADIUS = 0.6

# How close reachable ground has to be before a trunk counts as touchable, on
# top of the agent radius. Slack for the difference between this grid and the
# character controller's real footprint.
TOUCH_MARGIN = 0.5

# VisionSensor's radius, from Swordman's constructor. Past this an enemy cannot
# see the player at all, so a trunk further than this from both of them cannot
# be hiding one from the other.
VISION_RADIUS = 20.0

# Headings the sightline test sweeps. A line is symmetric, so these span 180
# degrees, and twelve of them puts a heading every 15.
SIGHT_HEADINGS = 12

WALL_PREFIXES = ("BOX_Boundary", "BOX_Fence", "BOX_Railing")
TREE_PREFIX = "BOX_Tree"


class PruneError(Exception):
    """The level is not in a shape this can reason about."""


def plan_corners(obstacle):
    """The box's four corners in the ground plane, with its yaw applied."""
    centre = obstacle["center"]
    half = obstacle["halfExtents"]
    yaw = math.radians(obstacle.get("yaw", 0.0))
    cos_y, sin_y = math.cos(yaw), math.sin(yaw)
    return [(centre[0] + sx * half[0] * cos_y + sz * half[2] * sin_y,
             centre[2] - sx * half[0] * sin_y + sz * half[2] * cos_y)
            for sx in (-1.0, 1.0) for sz in (-1.0, 1.0)]


def plan_bounds(points):
    return (min(p[0] for p in points), min(p[1] for p in points),
            max(p[0] for p in points), max(p[1] for p in points))


class Reachable:
    """Where the player can stand, as a grid flood-filled from the spawn."""

    def __init__(self, level):
        obstacles = level["obstacles"]
        walls = [o for o in obstacles
                 if o.get("name", "").startswith(WALL_PREFIXES)]
        boundary = [p for o in walls
                    if o.get("name", "").startswith("BOX_Boundary")
                    for p in plan_corners(o)]
        if not boundary:
            raise PruneError(
                "no BOX_Boundary_* proxies, so there is nothing marking what is "
                "enclosed. Without a boundary every trunk is arguably reachable "
                "and pruning would be guesswork.")

        self.x0, self.z0, x1, z1 = plan_bounds(boundary)
        self.nx = int((x1 - self.x0) / CELL) + 2
        self.nz = int((z1 - self.z0) / CELL) + 2

        blocked = bytearray(self.nx * self.nz)
        for wall in walls:
            ax, az, bx, bz = plan_bounds(plan_corners(wall))
            # Inflated by the agent radius: a gap the agent cannot fit through
            # is not a way past, and Recast erodes by the same amount.
            ax -= AGENT_RADIUS; az -= AGENT_RADIUS
            bx += AGENT_RADIUS; bz += AGENT_RADIUS
            for i, j in self._cells(ax, az, bx, bz):
                blocked[j * self.nx + i] = 1

        spawn = level["playerSpawn"]["position"]
        si, sj = self._index(spawn[0], spawn[2])
        if not (0 <= si < self.nx and 0 <= sj < self.nz):
            raise PruneError("the player spawn is outside the boundary rect.")
        if blocked[sj * self.nx + si]:
            raise PruneError(
                "the player spawn is inside a wall proxy once inflated by the "
                "agent radius, so the fill has nowhere to start.")

        self.seen = bytearray(self.nx * self.nz)
        self.seen[sj * self.nx + si] = 1
        queue = deque([(si, sj)])
        while queue:
            i, j = queue.popleft()
            for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                a, b = i + di, j + dj
                if (0 <= a < self.nx and 0 <= b < self.nz
                        and not self.seen[b * self.nx + a]
                        and not blocked[b * self.nx + a]):
                    self.seen[b * self.nx + a] = 1
                    queue.append((a, b))
        self.cells = sum(self.seen)

    def _index(self, x, z):
        return int((x - self.x0) / CELL), int((z - self.z0) / CELL)

    def _cells(self, ax, az, bx, bz):
        i0, j0 = self._index(ax, az)
        i1, j1 = self._index(bx, bz)
        for i in range(max(0, i0), min(self.nx, i1 + 1)):
            for j in range(max(0, j0), min(self.nz, j1 + 1)):
                yield i, j

    def at(self, x, z):
        i, j = self._index(x, z)
        return (0 <= i < self.nx and 0 <= j < self.nz
                and self.seen[j * self.nx + i])

    def near(self, ax, az, bx, bz, reach):
        for i, j in self._cells(ax - reach, az - reach, bx + reach, bz + reach):
            if self.seen[j * self.nx + i]:
                return True
        return False

    def can_block_sight(self, cx, cz):
        """Is reachable ground on both sides of this point, within vision?"""
        for k in range(SIGHT_HEADINGS):
            theta = math.pi * k / SIGHT_HEADINGS
            dx, dz = math.cos(theta), math.sin(theta)
            forward = backward = False
            step = CELL
            while step <= VISION_RADIUS:
                if not forward and self.at(cx + dx * step, cz + dz * step):
                    forward = True
                if not backward and self.at(cx - dx * step, cz - dz * step):
                    backward = True
                if forward and backward:
                    return True
                step += CELL
        return False


def classify(level):
    """(kept_touch, kept_sight, removable names) for the level's tree proxies."""
    reach = Reachable(level)
    trees = [o for o in level["obstacles"]
             if o.get("name", "").startswith(TREE_PREFIX)]

    touch, sight, removable = [], [], []
    for tree in trees:
        ax, az, bx, bz = plan_bounds(plan_corners(tree))
        if reach.near(ax, az, bx, bz, AGENT_RADIUS + TOUCH_MARGIN):
            touch.append(tree["name"])
        elif reach.can_block_sight((ax + bx) * 0.5, (az + bz) * 0.5):
            sight.append(tree["name"])
        else:
            removable.append(tree["name"])
    return reach, trees, touch, sight, removable


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    dry_run = "--dry-run" in sys.argv
    if len(args) != 1:
        raise SystemExit(
            "usage: python3 tools/prune_tree_proxies.py <level-dir> [--dry-run]")

    level_dir = args[0]
    path = os.path.join(level_dir, "level.json")
    with open(path) as f:
        level = json.load(f)

    reach, trees, touch, sight, removable = classify(level)

    print("%s" % level.get("name", level_dir))
    print("  reachable   %d cells (%.0f m^2) from the player spawn"
          % (reach.cells, reach.cells * CELL * CELL))
    print("  trees       %d proxies" % len(trees))
    print("    kept      %d the player can walk into" % len(touch))
    print("    kept      %d that can block a sightline" % len(sight))
    print("    removable %d" % len(removable))

    if not removable:
        print("  nothing to prune.")
        return 0

    before = len(level["obstacles"])
    drop = set(removable)
    level["obstacles"] = [o for o in level["obstacles"]
                          if o.get("name") not in drop]
    after = len(level["obstacles"])

    print("  obstacles   %d -> %d (-%.0f%%), %d fewer triangles into the "
          "navmesh bake" % (before, after, 100.0 * (before - after) / before,
                            (before - after) * 12))

    if dry_run:
        print("  dry run -- not written")
        return 0

    with open(path, "w") as f:
        json.dump(level, f, indent=2)
        f.write("\n")
    print("  wrote       %s" % path)
    print("  NOTE re-exporting this level restores the proxies. "
          "verify_level.py reports how many are prunable, so a level that has "
          "drifted back shows up there.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except PruneError as error:
        raise SystemExit("[prune_tree_proxies] %s" % error)
