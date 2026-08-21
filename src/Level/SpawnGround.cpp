#include <Level/SpawnGround.h>

#include <cmath>

namespace {

/// What PhysicsManager::classifySurfaceNormal calls ground, and what
/// tools/verify_level.py's spawn check uses. Kept equal to both on purpose: a
/// spawn snapped to something the physics would then treat as a wall is worse
/// than one that failed outright.
constexpr float kGroundNormalY = 0.4f;

/// How many downward casts before giving up.
///
/// One cast is not enough. CollisionMesh::groundBelow returns the *first*
/// surface below the origin, which under an arch is its downward-facing
/// underside -- so a rejected hit has to be stepped past and the cast repeated.
/// Eight is well past the deepest stack of overlapping surfaces in these maps.
constexpr int kMaxCasts = 8;

/// Nudge past a rejected hit so the next cast cannot find the same triangle.
constexpr float kStepPast = 0.01f;

} // namespace

namespace SpawnGround {

bool highestUnder(const CollisionMesh &mesh,
                  const std::vector<PhysicsObstacle> &obstacles,
                  const BoundingBox &bounds, float x, float z, float &out_y) {
    // Start above everything the level declares, not above the query point:
    // there is no query point yet, which is the entire reason this exists.
    const float ceiling = bounds.max.y + 10.0f;
    const float reach = (ceiling - bounds.min.y) + 20.0f;

    bool found = false;
    float best = 0.0f;

    if (!mesh.isEmpty()) {
        Vector3 origin{x, ceiling, z};
        for (int cast = 0; cast < kMaxCasts; ++cast) {
            MeshHit hit;
            if (!mesh.groundBelow(origin, reach, hit)) break;
            if (hit.normal.y > kGroundNormalY) {
                best = hit.point.y;
                found = true;
                break;
            }
            // A ceiling, a soffit, or the underside of a bridge. Drop below it
            // and look again.
            origin.y = hit.point.y - kStepPast;
            if (origin.y < bounds.min.y) break;
        }
    }

    // Proxies only when the mesh had no answer -- NOT as competing candidates.
    //
    // Letting them compete looked principled (it is what PhysicsManager does
    // when it decides what you are standing on) and was wrong here, measurably:
    // phase1 ships 523 BOX_ proxies, almost all of them tree trunks and the
    // boundary ring, so "highest standable thing over this x/z" put spawns on
    // top of trees. It is legal to stand there and nobody means it.
    //
    // On a level that ships a collision mesh, that mesh IS the ground -- the
    // exporter puts terrain, buildings, bridges and placed rocks in it, and
    // leaves the proxies to be the things you stand beside. On a level that
    // ships none (greybox, forest, the phase interiors) the proxies are all
    // there is, and then they are exactly right.
    //
    // The useful side effect: on format-2 levels this now agrees with
    // tools/verify_level.py's ground_under, which only ever sees the mesh. The
    // number check 5 prints is the number the engine will use.
    if (!found) {
        for (const PhysicsObstacle &obstacle : obstacles) {
            if (!obstacle.containsXZ({x, 0.0f, z})) continue;
            if (obstacle.getNormal().y < kGroundNormalY) continue;  // a wall
            const float top = obstacle.getHeightAt({x, 0.0f, z});
            if (!found || top > best) {
                best = top;
                found = true;
            }
        }
    }

    if (found) out_y = best;
    return found;
}

} // namespace SpawnGround
