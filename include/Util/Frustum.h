#pragma once

#include <raylib.h>
#include <raymath.h>

/// The six clip planes of a view-projection matrix, for AABB culling.
///
/// raylib has no frustum type and no culling of its own — `BeginMode3D` sets a
/// projection and every DrawMesh after it is submitted regardless of where the
/// camera is looking. That is invisible in a one-room arena and stops being
/// invisible the moment a level is large enough that most of it is behind you.
struct Frustum {
    /// Plane equations as (a, b, c, d) with a*x + b*y + c*z + d = 0, normals
    /// pointing *into* the frustum.
    Vector4 planes[6]{};

    /// Gribb-Hartmann: each plane is a row of the view-projection matrix added
    /// to or subtracted from the w row. Raylib's Matrix is row-major in its
    /// field names (m0..m15 with m12..m14 as translation), so the "rows" being
    /// combined here are the m0/m4/m8/m12-style strides.
    static Frustum fromViewProjection(const Matrix &vp) {
        Frustum f;
        auto row = [&](int i) {
            return Vector4{
                (i == 0) ? vp.m0 : (i == 1) ? vp.m1 : (i == 2) ? vp.m2 : vp.m3,
                (i == 0) ? vp.m4 : (i == 1) ? vp.m5 : (i == 2) ? vp.m6 : vp.m7,
                (i == 0) ? vp.m8 : (i == 1) ? vp.m9 : (i == 2) ? vp.m10 : vp.m11,
                (i == 0) ? vp.m12 : (i == 1) ? vp.m13 : (i == 2) ? vp.m14 : vp.m15};
        };
        const Vector4 x = row(0), y = row(1), z = row(2), w = row(3);

        f.planes[0] = add(w, x);  // left
        f.planes[1] = sub(w, x);  // right
        f.planes[2] = add(w, y);  // bottom
        f.planes[3] = sub(w, y);  // top
        f.planes[4] = add(w, z);  // near
        f.planes[5] = sub(w, z);  // far

        for (Vector4 &p : f.planes) p = normalize(p);
        return f;
    }

    /// Conservative AABB test: false only when the box is wholly outside.
    ///
    /// Uses the "positive vertex" trick — for each plane, only the box corner
    /// furthest along that plane's normal can keep the box inside, so one dot
    /// product per plane decides it instead of eight. A box straddling two
    /// planes' outside half-spaces without being outside either one on its own
    /// survives; that false positive costs a draw call, where a false negative
    /// would cost a hole in the world.
    bool intersects(const BoundingBox &box) const {
        for (const Vector4 &p : planes) {
            const Vector3 positive = {p.x >= 0.0f ? box.max.x : box.min.x,
                                      p.y >= 0.0f ? box.max.y : box.min.y,
                                      p.z >= 0.0f ? box.max.z : box.min.z};
            if (p.x * positive.x + p.y * positive.y + p.z * positive.z + p.w < 0.0f) {
                return false;
            }
        }
        return true;
    }

private:
    static Vector4 add(Vector4 a, Vector4 b) {
        return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
    }
    static Vector4 sub(Vector4 a, Vector4 b) {
        return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w};
    }
    static Vector4 normalize(Vector4 p) {
        const float length = sqrtf(p.x * p.x + p.y * p.y + p.z * p.z);
        if (length <= 0.0f) return p;
        return {p.x / length, p.y / length, p.z / length, p.w / length};
    }
};
