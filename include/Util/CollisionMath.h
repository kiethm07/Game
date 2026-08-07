#pragma once

#include <raylib.h>
#include <raymath.h>
#include <algorithm>
#include <cmath>
#include "Util/Sphere.h"
#include "Util/Capsule.h"

class CollisionMath {
public:
    // Distance squared between two line segments
    static float closestDistanceSqrBetweenLineSegments(Vector3 p1, Vector3 q1, Vector3 p2, Vector3 q2) {
        Vector3 d1 = Vector3Subtract(q1, p1);
        Vector3 d2 = Vector3Subtract(q2, p2);
        Vector3 r = Vector3Subtract(p1, p2);
        float a = Vector3DotProduct(d1, d1); // Length squared of segment 1
        float e = Vector3DotProduct(d2, d2); // Length squared of segment 2
        float f = Vector3DotProduct(d2, r);

        float s = 0.0f, t = 0.0f;
        float c = Vector3DotProduct(d1, r);
        float b = Vector3DotProduct(d1, d2);
        float denom = a * e - b * b; 

        if (denom != 0.0f) {
            s = std::clamp((b * f - c * e) / denom, 0.0f, 1.0f);
        } else {
            s = 0.0f;
        }

        float tnom = b * s + f;
        if (tnom < 0.0f) {
            t = 0.0f;
            s = (a > 0.0001f) ? std::clamp(-c / a, 0.0f, 1.0f) : 0.0f;
        } else if (tnom > e) {
            t = 1.0f;
            s = (a > 0.0001f) ? std::clamp((b - c) / a, 0.0f, 1.0f) : 0.0f;
        } else {
            t = (e > 0.0001f) ? (tnom / e) : 0.0f;
        }

        Vector3 c1 = Vector3Add(p1, Vector3Scale(d1, s));
        Vector3 c2 = Vector3Add(p2, Vector3Scale(d2, t));
        return Vector3DistanceSqr(c1, c2);
    }

    static bool checkCapsuleCapsule(const Capsule& c1, const Capsule& c2) {
        float dist_sq = closestDistanceSqrBetweenLineSegments(c1.getBase(), c1.getTip(), c2.getBase(), c2.getTip());
        float combined_radius = c1.getRadius() + c2.getRadius();
        return dist_sq <= (combined_radius * combined_radius);
    }

    // Check overlap between attack Sphere (HitBox) and character Capsule (HurtBox)
    static bool checkSphereCapsule(const Sphere& sphere, const Capsule& capsule) {
        Vector3 AB = Vector3Subtract(capsule.getTip(), capsule.getBase());
        Vector3 AC = Vector3Subtract(sphere.getCenter(), capsule.getBase());

        float ab_sq = Vector3LengthSqr(AB);
        float t = 0.0f;

        if (ab_sq > 0.0001f) {
            t = Vector3DotProduct(AC, AB) / ab_sq;
            t = std::clamp(t, 0.0f, 1.0f);
        }

        Vector3 closest_point = Vector3Add(capsule.getBase(), Vector3Scale(AB, t));
        float dist_sq = Vector3DistanceSqr(sphere.getCenter(), closest_point);
        float combined_radius = sphere.getRadius() + capsule.getRadius();

        return dist_sq <= (combined_radius * combined_radius);
    }

    // Resolve overlap between two cylinders (origin at the feet, extending upward).
    // The pair must overlap vertically to count as touching at all — otherwise
    // characters on different floors would shove each other. The push-out itself
    // stays on the XZ plane so separation never launches anyone vertically.
    static bool resolveCylinderCylinder(Vector3& pos_a, float radius_a, float height_a,
                                        Vector3& pos_b, float radius_b, float height_b,
                                        float weight_a = 0.5f, float weight_b = 0.5f) {
        // Vertical span test. The epsilon keeps a character standing exactly on
        // another's head (spans meeting at a single point) from registering.
        const float SPAN_EPS = 0.001f;
        if (pos_a.y >= pos_b.y + height_b - SPAN_EPS) return false;
        if (pos_b.y >= pos_a.y + height_a - SPAN_EPS) return false;

        Vector3 diff = Vector3Subtract(pos_a, pos_b);
        diff.y = 0.0f;

        float dist_sq = Vector3LengthSqr(diff);
        float combined_radius = radius_a + radius_b;

        if (dist_sq >= combined_radius * combined_radius) {
            return false;
        }

        float dist = std::sqrt(dist_sq);
        Vector3 push_dir = { 1.0f, 0.0f, 0.0f };
        if (dist > 0.0001f) {
            push_dir = Vector3Scale(diff, 1.0f / dist);
        }

        float overlap = combined_radius - dist;
        Vector3 push_a = Vector3Scale(push_dir, overlap * weight_a);
        Vector3 push_b = Vector3Scale(push_dir, overlap * weight_b);

        pos_a = Vector3Add(pos_a, push_a);
        pos_b = Vector3Subtract(pos_b, push_b);

        return true;
    }

    static bool resolveCylinderAABB(Vector3& char_pos, float radius, const BoundingBox& wall_box) {
        bool is_inside = (char_pos.x >= wall_box.min.x && char_pos.x <= wall_box.max.x &&
                          char_pos.z >= wall_box.min.z && char_pos.z <= wall_box.max.z);

        if (is_inside) {
            // Deep penetration (center is inside the box)
            // Eject them to the nearest face
            float dx1 = char_pos.x - wall_box.min.x;
            float dx2 = wall_box.max.x - char_pos.x;
            float dz1 = char_pos.z - wall_box.min.z;
            float dz2 = wall_box.max.z - char_pos.z;

            float min_d = std::min({dx1, dx2, dz1, dz2});
            Vector3 push_dir = {0, 0, 0};
            if (min_d == dx1) push_dir = {-1, 0, 0};
            else if (min_d == dx2) push_dir = {1, 0, 0};
            else if (min_d == dz1) push_dir = {0, 0, -1};
            else push_dir = {0, 0, 1};

            float penetration = radius + min_d;
            char_pos = Vector3Add(char_pos, Vector3Scale(push_dir, penetration));
            return true;
        }

        Vector3 closest_point;
        closest_point.x = std::clamp(char_pos.x, wall_box.min.x, wall_box.max.x);
        closest_point.y = char_pos.y;
        closest_point.z = std::clamp(char_pos.z, wall_box.min.z, wall_box.max.z);

        Vector3 diff = Vector3Subtract(char_pos, closest_point);
        diff.y = 0.0f;

        float dist_sq = Vector3LengthSqr(diff);
        if (dist_sq >= radius * radius) {
            return false;
        }

        float dist = std::sqrt(dist_sq);
        Vector3 push_dir = { 0.0f, 0.0f, 1.0f }; // fallback
        if (dist > 0.0001f) {
            push_dir = Vector3Scale(diff, 1.0f / dist);
        }

        float penetration = radius - dist;
        char_pos = Vector3Add(char_pos, Vector3Scale(push_dir, penetration));

        return true;
    }
};