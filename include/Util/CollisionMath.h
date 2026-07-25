#pragma once

#include <raylib.h>
#include <raymath.h>
#include <algorithm>
#include <cmath>
#include "Util/Sphere.h"
#include "Util/Capsule.h"

class CollisionMath {
public:
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

    // Resolve overlap between two cylinders on XZ plane
    static bool resolveCylinderCylinder(Vector3& pos_a, float radius_a, Vector3& pos_b, float radius_b) {
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
        Vector3 half_push = Vector3Scale(push_dir, overlap * 0.5f);

        pos_a = Vector3Add(pos_a, half_push);
        pos_b = Vector3Subtract(pos_b, half_push);

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