#include <Physics/CollisionMesh.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

/// Triangles per leaf. Small enough that a leaf test is a handful of
/// ray-triangle checks, large enough that the tree does not become mostly
/// interior nodes -- at 1 per leaf the node array costs more to walk than the
/// triangles it saves testing.
constexpr int kLeafSize = 8;

/// Guards against pathological splits (many coincident centroids) turning the
/// build into deep recursion. Past this a node becomes a leaf whatever its size.
constexpr int kMaxDepth = 32;

constexpr float kEpsilon = 1e-8f;

inline Vector3 sub(Vector3 a, Vector3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline Vector3 cross(Vector3 a, Vector3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

inline float dot(Vector3 a, Vector3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline float axis(Vector3 v, int i) {
    return i == 0 ? v.x : (i == 1 ? v.y : v.z);
}

BoundingBox emptyBounds() {
    const float inf = std::numeric_limits<float>::infinity();
    return {{inf, inf, inf}, {-inf, -inf, -inf}};
}

void growBounds(BoundingBox &box, Vector3 p) {
    box.min.x = std::fmin(box.min.x, p.x);
    box.min.y = std::fmin(box.min.y, p.y);
    box.min.z = std::fmin(box.min.z, p.z);
    box.max.x = std::fmax(box.max.x, p.x);
    box.max.y = std::fmax(box.max.y, p.y);
    box.max.z = std::fmax(box.max.z, p.z);
}

void growBounds(BoundingBox &box, const BoundingBox &other) {
    growBounds(box, other.min);
    growBounds(box, other.max);
}

bool boxesOverlap(const BoundingBox &a, const BoundingBox &b) {
    return a.min.x <= b.max.x && a.max.x >= b.min.x &&
           a.min.y <= b.max.y && a.max.y >= b.min.y &&
           a.min.z <= b.max.z && a.max.z >= b.min.z;
}

/// Slab test returning the near intersection distance.
///
/// The NaN handling here is the whole point, and getting it wrong leaks rays
/// through solid geometry. For a ray with a zero direction component the
/// reciprocal is +-infinity, and if the origin also lies exactly on that slab's
/// boundary the product is `0 * infinity` -- NaN, not infinity. A downward
/// ground probe has two zero components, and BVH node bounds line up exactly
/// with the grid the terrain was built on, so "origin exactly on a boundary" is
/// not an edge case: it is what happens whenever the character stands on a
/// vertex or a cell edge. Measured on a regular 0.5 m grid, a fmin/fmax
/// formulation dropped 17% of such probes -- the character falling through the
/// floor for no visible reason.
///
/// The fix is to compare rather than call fmin/fmax, and to let the *old* value
/// win. Every comparison against NaN is false, so a NaN bound leaves tmin/tmax
/// untouched and the axis is simply skipped -- which is the right answer for a
/// ray that runs parallel to those planes and starts between them.
bool rayHitsBox(Vector3 origin, Vector3 inv_dir, float max_distance,
                const BoundingBox &box, float &t_near) {
    float tmin = 0.0f;
    float tmax = max_distance;

    const float lo[3] = {box.min.x, box.min.y, box.min.z};
    const float hi[3] = {box.max.x, box.max.y, box.max.z};
    const float org[3] = {origin.x, origin.y, origin.z};
    const float inv[3] = {inv_dir.x, inv_dir.y, inv_dir.z};

    for (int a = 0; a < 3; ++a) {
        float t0 = (lo[a] - org[a]) * inv[a];
        float t1 = (hi[a] - org[a]) * inv[a];
        if (t0 > t1) {
            const float swap = t0;
            t0 = t1;
            t1 = swap;
        }
        if (t0 > tmin) tmin = t0;
        if (t1 < tmax) tmax = t1;
        if (tmax < tmin) return false;
    }

    t_near = tmin;
    return true;
}

/// How far outside a triangle a barycentric coordinate may sit and still count
/// as a hit.
///
/// Defensive, and honestly not load-bearing against any case currently
/// measured: with it set to zero the watertightness test still reports no
/// leaks. It is kept because the failure it guards against is real in general
/// -- a ray through the edge two triangles share is evaluated against each
/// independently, and rounding can put `u` or `v` a hair outside [0, 1] on both
/// at once, so neither claims the hit -- and because the meshes this will
/// actually carry are decimated terrain full of slivers, which is where that
/// conditioning is worst. A double hit costs nothing (the caller keeps the
/// nearer); a leak drops the character through the floor.
///
/// Worth being clear about what did *not* cause the leak found while writing
/// this: it was rayHitsBox, not this test. See the note there.
constexpr float kEdgeTolerance = 1e-5f;

/// Moller-Trumbore, double-sided.
///
/// Backfaces are deliberately not culled. A collision mesh is authored as a
/// shell -- the castle terrain has its underside deleted entirely -- so a query
/// that starts inside geometry, or a wall sampled from the far side, has to
/// report the surface rather than pass through it and hand back whatever lies
/// beyond.
bool rayHitsTriangle(Vector3 origin, Vector3 direction, Vector3 a, Vector3 b,
                     Vector3 c, float &t) {
    const Vector3 edge1 = sub(b, a);
    const Vector3 edge2 = sub(c, a);
    const Vector3 h = cross(direction, edge2);
    const float det = dot(edge1, h);
    if (std::fabs(det) < kEpsilon) return false;

    const float inv_det = 1.0f / det;
    const Vector3 s = sub(origin, a);
    const float u = inv_det * dot(s, h);
    if (u < -kEdgeTolerance || u > 1.0f + kEdgeTolerance) return false;

    const Vector3 q = cross(s, edge1);
    const float v = inv_det * dot(direction, q);
    if (v < -kEdgeTolerance || u + v > 1.0f + kEdgeTolerance) return false;

    t = inv_det * dot(edge2, q);
    return t > kEpsilon;
}

} // namespace

// ---------------------------------------------------------------------------
// Build
// ---------------------------------------------------------------------------

void CollisionMesh::build(std::vector<Vector3> vertices,
                          std::vector<int> indices) {
    verts = std::move(vertices);
    tris.clear();
    normals.clear();
    centroids.clear();
    order.clear();
    nodes.clear();
    degenerate_count = 0;

    const int source_triangles = static_cast<int>(indices.size() / 3);
    tris.reserve(indices.size());
    normals.reserve(source_triangles);
    centroids.reserve(source_triangles);

    for (int i = 0; i < source_triangles; ++i) {
        const int ia = indices[i * 3 + 0];
        const int ib = indices[i * 3 + 1];
        const int ic = indices[i * 3 + 2];
        if (ia < 0 || ib < 0 || ic < 0 ||
            ia >= static_cast<int>(verts.size()) ||
            ib >= static_cast<int>(verts.size()) ||
            ic >= static_cast<int>(verts.size())) {
            degenerate_count++;
            continue;
        }

        const Vector3 a = verts[ia];
        const Vector3 b = verts[ib];
        const Vector3 c = verts[ic];
        const Vector3 n = cross(sub(b, a), sub(c, a));
        const float len = std::sqrt(dot(n, n));
        if (len < kEpsilon) {
            degenerate_count++;
            continue;
        }

        tris.push_back(ia);
        tris.push_back(ib);
        tris.push_back(ic);
        normals.push_back({n.x / len, n.y / len, n.z / len});
        centroids.push_back({(a.x + b.x + c.x) / 3.0f,
                             (a.y + b.y + c.y) / 3.0f,
                             (a.z + b.z + c.z) / 3.0f});
    }

    triangle_count = static_cast<int>(normals.size());
    order.resize(triangle_count);
    for (int i = 0; i < triangle_count; ++i) order[i] = i;

    if (triangle_count > 0) {
        nodes.reserve(std::max(1, triangle_count * 2 / kLeafSize));
        buildNode(0, triangle_count, 0);
    }
}

BoundingBox CollisionMesh::triangleBounds(int triangle) const {
    BoundingBox box = emptyBounds();
    growBounds(box, verts[tris[triangle * 3 + 0]]);
    growBounds(box, verts[tris[triangle * 3 + 1]]);
    growBounds(box, verts[tris[triangle * 3 + 2]]);
    return box;
}

int CollisionMesh::buildNode(int start, int count, int depth) {
    const int index = static_cast<int>(nodes.size());
    nodes.push_back(Node{});

    BoundingBox bounds = emptyBounds();
    for (int i = 0; i < count; ++i) {
        growBounds(bounds, triangleBounds(order[start + i]));
    }
    nodes[index].bounds = bounds;

    if (count <= kLeafSize || depth >= kMaxDepth) {
        nodes[index].start = start;
        nodes[index].count = count;
        return index;
    }

    // Split on the widest axis of the *centroid* spread rather than of the
    // bounds. Bounds width is inflated by any single large triangle, which on
    // terrain (long thin slivers at the rim) picks an axis that barely
    // separates anything.
    BoundingBox spread = emptyBounds();
    for (int i = 0; i < count; ++i) growBounds(spread, centroids[order[start + i]]);
    const Vector3 extent = sub(spread.max, spread.min);
    int split_axis = 0;
    if (extent.y > extent.x) split_axis = 1;
    if (axis(extent, 2) > axis(extent, split_axis)) split_axis = 2;

    const int mid = count / 2;
    std::nth_element(order.begin() + start, order.begin() + start + mid,
                     order.begin() + start + count,
                     [&](int lhs, int rhs) {
                         return axis(centroids[lhs], split_axis) <
                                axis(centroids[rhs], split_axis);
                     });

    // Every centroid identical on this axis: nth_element cannot separate them
    // and recursing would repeat this node forever. Take it as a leaf.
    if (axis(extent, split_axis) < kEpsilon) {
        nodes[index].start = start;
        nodes[index].count = count;
        return index;
    }

    const int left = buildNode(start, mid, depth + 1);
    const int right = buildNode(start + mid, count - mid, depth + 1);
    nodes[index].left = left;
    nodes[index].right = right;
    nodes[index].count = 0;
    return index;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

BoundingBox CollisionMesh::getBounds() const {
    if (nodes.empty()) return BoundingBox{{0, 0, 0}, {0, 0, 0}};
    return nodes[0].bounds;
}

void CollisionMesh::getTriangle(int index, Vector3 &a, Vector3 &b,
                                Vector3 &c) const {
    a = verts[tris[index * 3 + 0]];
    b = verts[tris[index * 3 + 1]];
    c = verts[tris[index * 3 + 2]];
}

bool CollisionMesh::raycast(Vector3 origin, Vector3 direction,
                            float max_distance, MeshHit &out) const {
    if (nodes.empty()) return false;

    const Vector3 inv_dir = {1.0f / direction.x, 1.0f / direction.y,
                             1.0f / direction.z};

    float best_t = max_distance;
    int best_triangle = -1;

    int stack[64];
    int depth = 0;
    stack[depth++] = 0;

    while (depth > 0) {
        const Node &node = nodes[stack[--depth]];

        float t_near = 0.0f;
        if (!rayHitsBox(origin, inv_dir, best_t, node.bounds, t_near)) continue;
        // The box may have been entered before the hit found so far, but if the
        // whole box starts beyond it there is nothing closer inside.
        if (t_near > best_t) continue;

        if (node.count > 0) {
            for (int i = 0; i < node.count; ++i) {
                const int triangle = order[node.start + i];
                Vector3 a, b, c;
                getTriangle(triangle, a, b, c);
                float t = 0.0f;
                if (rayHitsTriangle(origin, direction, a, b, c, t) &&
                    t < best_t) {
                    best_t = t;
                    best_triangle = triangle;
                }
            }
            continue;
        }

        if (depth + 2 <= static_cast<int>(sizeof(stack) / sizeof(stack[0]))) {
            stack[depth++] = node.left;
            stack[depth++] = node.right;
        }
    }

    if (best_triangle < 0) return false;

    out.triangle = best_triangle;
    out.distance = best_t;
    out.normal = normals[best_triangle];
    out.point = {origin.x + direction.x * best_t, origin.y + direction.y * best_t,
                 origin.z + direction.z * best_t};
    return true;
}

bool CollisionMesh::groundBelow(Vector3 point, float max_drop,
                                MeshHit &out) const {
    return raycast(point, {0.0f, -1.0f, 0.0f}, max_drop, out);
}

void CollisionMesh::overlapAABB(const BoundingBox &box,
                                std::vector<int> &out) const {
    if (nodes.empty()) return;

    int stack[64];
    int depth = 0;
    stack[depth++] = 0;

    while (depth > 0) {
        const Node &node = nodes[stack[--depth]];
        if (!boxesOverlap(node.bounds, box)) continue;

        if (node.count > 0) {
            for (int i = 0; i < node.count; ++i) {
                const int triangle = order[node.start + i];
                if (boxesOverlap(triangleBounds(triangle), box)) {
                    out.push_back(triangle);
                }
            }
            continue;
        }

        if (depth + 2 <= static_cast<int>(sizeof(stack) / sizeof(stack[0]))) {
            stack[depth++] = node.left;
            stack[depth++] = node.right;
        }
    }
}
