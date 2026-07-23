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
};