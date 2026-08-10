// Standalone checks for CollisionMesh against hand-computed answers.
//
// Deliberately outside CMake -- the project has no test framework and CMake
// globs src/*.cpp, so a second main() in there would break the build. Compile
// and run it by hand:
//
//   c++ -std=c++17 -O2 -Wall -Wextra \
//       -I include -I build/_deps/raylib-src/src \
//       tools/test_collision_mesh.cpp src/Physics/CollisionMesh.cpp -o /tmp/tcm \
//     && /tmp/tcm
//
// Section 7 is the one that matters most: it fires rays straight down through
// exact vertices, cell edges and quad diagonals, which is where a ray-AABB slab
// test computes 0 * infinity and silently drops the character through the
// floor. It caught exactly that, at a 17% leak rate.
#include <Physics/CollisionMesh.h>

#include <cmath>
#include <cstdio>
#include <ctime>
#include <vector>

static int failures = 0;
static int checks = 0;

static void check(bool ok, const char *what) {
    checks++;
    if (!ok) {
        failures++;
        printf("  FAIL  %s\n", what);
    } else {
        printf("  ok    %s\n", what);
    }
}

static void nearly(float got, float want, float tol, const char *what) {
    checks++;
    if (std::fabs(got - want) > tol) {
        failures++;
        printf("  FAIL  %-46s got %.5f want %.5f\n", what, got, want);
    } else {
        printf("  ok    %-46s %.5f\n", what, got);
    }
}

// A flat 10x10 quad at y = 2, spanning x,z in [-5,5], wound counter-clockwise
// seen from above so its normal is +Y.
static void flatQuad(std::vector<Vector3> &v, std::vector<int> &i) {
    v = {{-5, 2, -5}, {5, 2, -5}, {5, 2, 5}, {-5, 2, 5}};
    i = {0, 2, 1, 0, 3, 2};
}

int main() {
    printf("\n1. Flat quad: build, bounds, degenerate rejection\n");
    {
        std::vector<Vector3> v;
        std::vector<int> idx;
        flatQuad(v, idx);
        // Append a degenerate triangle (two identical corners) and an
        // out-of-range index; both must be dropped, not stored.
        idx.insert(idx.end(), {0, 1, 1});
        idx.insert(idx.end(), {0, 1, 99});

        CollisionMesh mesh;
        mesh.build(v, idx);
        check(mesh.getTriangleCount() == 2, "2 triangles kept");
        check(mesh.getDegenerateCount() == 2, "2 bad triangles dropped");
        BoundingBox b = mesh.getBounds();
        nearly(b.min.y, 2.0f, 1e-5f, "bounds.min.y");
        nearly(b.max.x, 5.0f, 1e-5f, "bounds.max.x");
        check(!mesh.isEmpty(), "not empty");
    }

    printf("\n2. Axis-aligned downward ray (the slab-test edge case)\n");
    {
        std::vector<Vector3> v;
        std::vector<int> idx;
        flatQuad(v, idx);
        CollisionMesh mesh;
        mesh.build(v, idx);

        MeshHit hit;
        // Straight down from 10 units up: two direction components are exactly
        // zero, which is what a naive slab test divides by.
        check(mesh.raycast({1.0f, 12.0f, -2.0f}, {0, -1, 0}, 100.0f, hit),
              "vertical ray hits");
        nearly(hit.distance, 10.0f, 1e-4f, "distance to quad");
        nearly(hit.point.y, 2.0f, 1e-4f, "hit.point.y");
        nearly(hit.normal.y, 1.0f, 1e-4f, "normal points up");

        MeshHit ground;
        check(mesh.groundBelow({0.5f, 6.0f, 0.5f}, 10.0f, ground),
              "groundBelow finds the quad");
        nearly(ground.distance, 4.0f, 1e-4f, "groundBelow distance");

        check(!mesh.groundBelow({0.5f, 6.0f, 0.5f}, 1.0f, ground),
              "groundBelow respects max_drop");
        check(!mesh.raycast({20.0f, 12.0f, 0.0f}, {0, -1, 0}, 100.0f, hit),
              "ray outside the quad misses");
        check(!mesh.raycast({1.0f, 12.0f, 0.0f}, {0, 1, 0}, 100.0f, hit),
              "ray pointing away misses");
    }

    printf("\n3. Nearest hit wins across stacked surfaces\n");
    {
        // Three stacked quads at y = 0, 2, 4.
        std::vector<Vector3> v;
        std::vector<int> idx;
        for (int level = 0; level < 3; ++level) {
            const float y = level * 2.0f;
            const int base = level * 4;
            v.push_back({-5, y, -5});
            v.push_back({5, y, -5});
            v.push_back({5, y, 5});
            v.push_back({-5, y, 5});
            idx.insert(idx.end(), {base, base + 2, base + 1});
            idx.insert(idx.end(), {base, base + 3, base + 2});
        }
        CollisionMesh mesh;
        mesh.build(v, idx);
        check(mesh.getTriangleCount() == 6, "6 triangles");

        MeshHit hit;
        mesh.raycast({0, 10, 0}, {0, -1, 0}, 100.0f, hit);
        nearly(hit.point.y, 4.0f, 1e-4f, "top surface from above");
        mesh.raycast({0, 3, 0}, {0, -1, 0}, 100.0f, hit);
        nearly(hit.point.y, 2.0f, 1e-4f, "middle surface from between");
        mesh.raycast({0, -10, 0}, {0, 1, 0}, 100.0f, hit);
        nearly(hit.point.y, 0.0f, 1e-4f, "bottom surface from below (backface)");
    }

    printf("\n4. Curved ground: dome sampled against its analytic height\n");
    {
        // Triangulated hemisphere of radius 8 centred at the origin -- the case
        // a box staircase cannot represent at all.
        const int RINGS = 24, SEGS = 48;
        const float R = 8.0f;
        std::vector<Vector3> v;
        std::vector<int> idx;
        for (int r = 0; r <= RINGS; ++r) {
            const float phi = (float)r / RINGS * (3.14159265f * 0.5f);
            for (int s = 0; s <= SEGS; ++s) {
                const float th = (float)s / SEGS * 2.0f * 3.14159265f;
                v.push_back({R * std::sin(phi) * std::cos(th),
                             R * std::cos(phi),
                             R * std::sin(phi) * std::sin(th)});
            }
        }
        auto at = [&](int r, int s) { return r * (SEGS + 1) + s; };
        for (int r = 0; r < RINGS; ++r) {
            for (int s = 0; s < SEGS; ++s) {
                idx.insert(idx.end(), {at(r, s), at(r, s + 1), at(r + 1, s)});
                idx.insert(idx.end(), {at(r + 1, s), at(r, s + 1), at(r + 1, s + 1)});
            }
        }
        CollisionMesh mesh;
        mesh.build(v, idx);
        printf("  (%d triangles, %d BVH nodes)\n", mesh.getTriangleCount(),
               mesh.getNodeCount());

        float worst = 0.0f;
        for (float x = -6.0f; x <= 6.0f; x += 0.7f) {
            for (float z = -6.0f; z <= 6.0f; z += 0.7f) {
                const float rr = x * x + z * z;
                if (rr > 36.0f) continue;
                MeshHit hit;
                if (!mesh.groundBelow({x, 20.0f, z}, 60.0f, hit)) {
                    failures++; checks++;
                    printf("  FAIL  no ground at (%.1f, %.1f)\n", x, z);
                    continue;
                }
                worst = std::fmax(worst, std::fabs(hit.point.y -
                                                   std::sqrt(64.0f - rr)));
            }
        }
        // Tessellation error only -- a 24-ring dome chords its own curve.
        nearly(worst, 0.0f, 0.02f, "max error vs analytic dome height");
    }

    printf("\n5. overlapAABB\n");
    {
        std::vector<Vector3> v;
        std::vector<int> idx;
        flatQuad(v, idx);
        CollisionMesh mesh;
        mesh.build(v, idx);

        std::vector<int> hits;
        mesh.overlapAABB({{-10, 1, -10}, {10, 3, 10}}, hits);
        check(hits.size() == 2, "box over the whole quad finds both triangles");

        hits.clear();
        mesh.overlapAABB({{-10, 5, -10}, {10, 6, 10}}, hits);
        check(hits.empty(), "box above the quad finds nothing");

        hits.clear();
        mesh.overlapAABB({{-4.9f, 1.9f, -4.9f}, {-4.8f, 2.1f, -4.8f}}, hits);
        check(!hits.empty(), "tiny box on a corner finds a triangle");

        hits.clear();
        mesh.overlapAABB({{-10, 1, -10}, {10, 3, 10}}, hits);
        mesh.overlapAABB({{-10, 1, -10}, {10, 3, 10}}, hits);
        check(hits.size() == 4, "overlapAABB appends rather than clears");
    }

    printf("\n6. Empty mesh is safe\n");
    {
        CollisionMesh mesh;
        check(mesh.isEmpty(), "default is empty");
        MeshHit hit;
        check(!mesh.raycast({0, 1, 0}, {0, -1, 0}, 10.0f, hit), "raycast false");
        std::vector<int> hits;
        mesh.overlapAABB({{-1, -1, -1}, {1, 1, 1}}, hits);
        check(hits.empty(), "overlapAABB yields nothing");
        mesh.build({}, {});
        check(mesh.isEmpty(), "built from nothing is still empty");
    }

    printf("\n7. Watertightness: rays down shared edges and vertices\n");
    {
        // Regular grid on exact 0.5 m spacing, so every probe below lands
        // precisely on a cell boundary, a shared quad diagonal, or a vertex --
        // the three places a naive Moller-Trumbore leaks through.
        const int N = 40;
        const float STEP = 0.5f;
        std::vector<Vector3> v;
        std::vector<int> idx;
        for (int j = 0; j <= N; ++j)
            for (int i = 0; i <= N; ++i)
                v.push_back({i * STEP, 1.0f, j * STEP});
        for (int j = 0; j < N; ++j) {
            for (int i = 0; i < N; ++i) {
                const int a = j * (N + 1) + i;
                idx.insert(idx.end(), {a, a + N + 1, a + 1});
                idx.insert(idx.end(), {a + 1, a + N + 1, a + N + 2});
            }
        }
        CollisionMesh mesh;
        mesh.build(v, idx);

        int leaks = 0, probes = 0;
        for (int j = 1; j < N; ++j) {
            for (int i = 1; i < N; ++i) {
                const float x = i * STEP, z = j * STEP;
                // exact vertex, exact cell edge, and exact quad diagonal
                const float px[3] = {x, x, x + STEP * 0.5f};
                const float pz[3] = {z, z + STEP * 0.5f, z + STEP * 0.5f};
                for (int k = 0; k < 3; ++k) {
                    MeshHit hit;
                    probes++;
                    if (!mesh.groundBelow({px[k], 5.0f, pz[k]}, 10.0f, hit))
                        leaks++;
                }
            }
        }
        printf("  %d probes on exact edges/vertices, %d leaked\n", probes, leaks);
        check(leaks == 0, "no ray leaks through a shared edge");
    }

    printf("\n8. Cost at castle scale (~90k triangles)\n");
    {
        // A 210x210 grid of quads with a bumpy height, ~88k triangles: the same
        // order as the real castle collision mesh will be.
        const int N = 210;
        std::vector<Vector3> v;
        std::vector<int> idx;
        v.reserve((N + 1) * (N + 1));
        for (int j = 0; j <= N; ++j) {
            for (int i = 0; i <= N; ++i) {
                const float x = (i - N * 0.5f) * 0.5f;
                const float z = (j - N * 0.5f) * 0.5f;
                v.push_back({x, 3.0f * std::sin(x * 0.15f) * std::cos(z * 0.15f), z});
            }
        }
        for (int j = 0; j < N; ++j) {
            for (int i = 0; i < N; ++i) {
                const int a = j * (N + 1) + i;
                idx.insert(idx.end(), {a, a + N + 1, a + 1});
                idx.insert(idx.end(), {a + 1, a + N + 1, a + N + 2});
            }
        }

        CollisionMesh mesh;
        clock_t t0 = clock();
        mesh.build(v, idx);
        const double build_ms = 1000.0 * (clock() - t0) / CLOCKS_PER_SEC;
        printf("  %d triangles, %d nodes, build %.1f ms\n",
               mesh.getTriangleCount(), mesh.getNodeCount(), build_ms);

        // Ground probes: what PhysicsManager does once per character per frame.
        const int PROBES = 20000;
        t0 = clock();
        int found = 0;
        for (int k = 0; k < PROBES; ++k) {
            const float x = std::fmod(k * 0.37f, 40.0f) - 20.0f;
            const float z = std::fmod(k * 0.71f, 40.0f) - 20.0f;
            MeshHit hit;
            if (mesh.groundBelow({x, 30.0f, z}, 60.0f, hit)) found++;
        }
        const double probe_us = 1e6 * (clock() - t0) / CLOCKS_PER_SEC / PROBES;
        printf("  groundBelow  %.2f us/query (%d/%d hit)\n", probe_us, found, PROBES);
        check(found == PROBES, "every probe found ground");

        // Overlap: what the depenetration pass does per sub-step.
        const int OVERLAPS = 20000;
        std::vector<int> buffer;
        t0 = clock();
        size_t total = 0;
        for (int k = 0; k < OVERLAPS; ++k) {
            const float x = std::fmod(k * 0.37f, 40.0f) - 20.0f;
            const float z = std::fmod(k * 0.71f, 40.0f) - 20.0f;
            buffer.clear();
            mesh.overlapAABB({{x - 0.6f, -10.0f, z - 0.6f},
                              {x + 0.6f, 10.0f, z + 0.6f}}, buffer);
            total += buffer.size();
        }
        const double ov_us = 1e6 * (clock() - t0) / CLOCKS_PER_SEC / OVERLAPS;
        printf("  overlapAABB  %.2f us/query, %.1f triangles returned\n",
               ov_us, (double)total / OVERLAPS);
        check(probe_us < 20.0, "ground probe under 20 us");
        check(ov_us < 20.0, "overlap query under 20 us");
    }

    printf("\n%d checks, %d failures\n\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
