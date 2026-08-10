#pragma once

#include <raylib.h>

#include <vector>

/// Where a query met the mesh.
struct MeshHit {
    Vector3 point{0.0f, 0.0f, 0.0f};

    /// Geometric normal of the triangle that was hit, as wound in the source
    /// mesh. Not flipped to face the query: PhysicsManager classifies a surface
    /// by the sign of normal.y (classifySurfaceNormal), so a mesh whose faces
    /// are wound inward would report its floors as ceilings. Collision meshes
    /// therefore have to be exported with outward normals, and
    /// tools/verify_level.py is where that gets checked.
    Vector3 normal{0.0f, 1.0f, 0.0f};

    float distance = 0.0f;
    int triangle = -1;
};

/// Static world geometry as a triangle soup with an AABB tree over it.
///
/// This is what lets the world be something other than boxes. PhysicsObstacle
/// can express an axis-aligned box and a quad sloping along one axis, and
/// nothing else -- so curved ground, a round tower or an arch had to be
/// approximated by a staircase of boxes, which is both wrong and expensive. A
/// mesh represents them exactly and costs one BVH descent to query.
///
/// Deliberately independent of the rest of the engine: it uses raylib's vector
/// types but calls none of its functions, so ray-triangle and ray-box are
/// implemented here rather than borrowed from GetRayCollision*. That keeps the
/// class compilable and testable on its own, and means the ray tests can be the
/// ones this code actually wants -- nearest-hit, no backface culling, and a
/// slab test that tolerates axis-parallel rays.
class CollisionMesh {
public:
    /// Build from a triangle soup already in world space.
    ///
    /// `indices` is three entries per triangle. Degenerate triangles (zero
    /// area) are dropped rather than stored: their normals are meaningless and
    /// a zero-area triangle can never be the nearest hit, but it would still
    /// cost a ray test on every query that reached its leaf.
    void build(std::vector<Vector3> vertices, std::vector<int> indices);

    bool isEmpty() const { return triangle_count == 0; }
    int getTriangleCount() const { return triangle_count; }
    int getNodeCount() const { return static_cast<int>(nodes.size()); }
    int getDegenerateCount() const { return degenerate_count; }
    BoundingBox getBounds() const;

    /// Nearest intersection along `direction` within `max_distance`.
    ///
    /// `direction` need not be normalised; `max_distance` is measured in the
    /// same units the direction is given in only when it is. Returns false and
    /// leaves `out` untouched when nothing is hit.
    bool raycast(Vector3 origin, Vector3 direction, float max_distance,
                 MeshHit &out) const;

    /// The first surface at or below `point`, searching down `max_drop`.
    bool groundBelow(Vector3 point, float max_drop, MeshHit &out) const;

    /// Indices of every triangle whose bounds overlap `box`.
    ///
    /// Appends rather than clears, so a caller can accumulate candidates from
    /// several queries into one buffer and keep reusing its allocation.
    void overlapAABB(const BoundingBox &box, std::vector<int> &out) const;

    void getTriangle(int index, Vector3 &a, Vector3 &b, Vector3 &c) const;
    Vector3 getNormal(int index) const { return normals[index]; }

private:
    /// Leaves store a run of `count` triangles starting at `start` in `order`;
    /// interior nodes have count == 0 and index their children instead.
    struct Node {
        BoundingBox bounds{};
        int start = 0;
        int count = 0;
        int left = -1;
        int right = -1;
    };

    int buildNode(int start, int count, int depth);
    BoundingBox triangleBounds(int triangle) const;

    std::vector<Vector3> verts;
    std::vector<int> tris;          ///< 3 per triangle, indices into `verts`
    std::vector<Vector3> normals;   ///< one per triangle
    std::vector<Vector3> centroids; ///< one per triangle
    std::vector<int> order;         ///< triangle ids, permuted by the build
    std::vector<Node> nodes;

    int triangle_count = 0;
    int degenerate_count = 0;
};
