#pragma once

#include <Components/PhysicsObstacle.h>
#include <Physics/CollisionMesh.h>
#include <raylib.h>
#include <vector>

namespace SpawnGround {

/// The highest standable surface over (x, z).
///
/// Deliberately NOT the query PhysicsManager::probeMeshGround runs, and the
/// difference is the whole contract. That one starts at a character who already
/// has a height, so it finds the floor that character is standing above. This
/// one has no height to start from, so it starts above the level's bounds and
/// takes the first up-facing thing it meets on the way down.
///
/// Under an arch or a bridge that is the deck, not the ground beneath it. That
/// is the right answer for an x/z picked off an overhead view or read out of
/// the game with F4 -- and it is exactly why `y` stays optional in
/// enemies.json, for the case where it is not.
/// tools/make_castle_level.py describes this same trap from the other side.
///
/// The mesh and the BOX_/RAMP_ proxies contribute candidates on the same terms,
/// so a format-1 level that ships no collision mesh still snaps against its
/// proxies rather than silently answering 0.
///
/// Returns false when nothing standable is over (x, z) at all, leaving `out_y`
/// untouched. Callers must not substitute a height of their own: a spawn with
/// no ground under it is over a hole or outside the map, and 0 is a fiction
/// that looks playable.
bool highestUnder(const CollisionMesh &mesh,
                  const std::vector<PhysicsObstacle> &obstacles,
                  const BoundingBox &bounds, float x, float z, float &out_y);

} // namespace SpawnGround
