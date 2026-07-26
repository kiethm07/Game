#include <GameManager/PhysicsManager.h>
#include <raymath.h>
#include <limits>

// ---------------------------------------------------------------------------
// Character-vs-Character push-out (XZ plane only)
// ---------------------------------------------------------------------------
void PhysicsManager::resolveCharacterCollisions(const std::vector<Character*>& characters, std::vector<Vector3>& positions) {
    size_t count = characters.size();
    if (count < 2) return;

    for (size_t i = 0; i < count - 1; ++i) {
        Character* char_a = characters[i];
        assert(char_a != nullptr);

        for (size_t j = i + 1; j < count; ++j) {
            Character* char_b = characters[j];
            assert(char_b != nullptr);

            if (char_a->getStats().isDead() || char_b->getStats().isDead()) {
                continue;
            }

            Vector3 pos_a    = positions[i];
            Vector3 pos_b    = positions[j];
            float   radius_a = char_a->getColliderRadius();
            float   radius_b = char_b->getColliderRadius();

            bool resolved = CollisionMath::resolveCylinderCylinder(pos_a, radius_a, pos_b, radius_b);
            if (resolved) {
                positions[i] = pos_a;
                positions[j] = pos_b;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Headroom clearance — checks whether snapping feet to target_y would bury
// the character's head inside an obstacle.
//
// Fix (Bug 3 Root Cause 2): accepts the live integrated pos, radius, and
// height instead of pulling the stale pre-frame position from character->getPosition().
// ---------------------------------------------------------------------------
bool PhysicsManager::checkHeadroomClearance(
    Vector3 current_pos,
    float   radius,
    float   height,
    float   target_y,
    const std::vector<PhysicsObstacle>& obstacles) const
{
    BoundingBox target_box;
    target_box.min = { current_pos.x - radius, target_y + 0.1f,  current_pos.z - radius };
    target_box.max = { current_pos.x + radius, target_y + height, current_pos.z + radius };

    for (const PhysicsObstacle& obs : obstacles) {
        BoundingBox obox = obs.getApproxBox();
        if (CheckCollisionBoxes(target_box, obox)) {
            // Only block if the obstacle is genuinely above the current feet level
            // (i.e. it would press down on the character's head, not something the
            // character is already standing on top of).
            if (obox.min.y >= current_pos.y + 0.1f) {
                return false;
            }
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Classify the surface type from an outward-facing surface normal.
// ---------------------------------------------------------------------------
SurfaceType PhysicsManager::classifySurfaceNormal(const Vector3& normal) const {
    if (normal.y >  0.5f) return SurfaceType::GROUND_SURF;
    if (normal.y < -0.2f) return SurfaceType::CEILING_SURF;
    return SurfaceType::WALL_SURF;
}

// ---------------------------------------------------------------------------
// resolveEnvironmentCollisions — ceiling push-back and XZ wall ejection.
// Used outside the main physics loop (e.g. post-teleport corrections).
// ---------------------------------------------------------------------------
void PhysicsManager::resolveEnvironmentCollisions(
    const std::vector<Character*>& characters,
    const std::vector<PhysicsObstacle>& obstacles)
{
    for (Character* character : characters) {
        assert(character != nullptr);
        if (character->getStats().isDead()) {
            continue;
        }

        Vector3 char_pos = character->getPosition();
        float   radius   = character->getColliderRadius();
        float   height   = character->getColliderHeight();

        for (const PhysicsObstacle& obs : obstacles) {
            BoundingBox obox        = obs.getApproxBox();
            Vector3     char_center = { char_pos.x, char_pos.y + height * 0.5f, char_pos.z };

            Vector3 closest_point;
            closest_point.x = std::clamp(char_center.x, obox.min.x, obox.max.x);
            closest_point.y = std::clamp(char_center.y, obox.min.y, obox.max.y);
            closest_point.z = std::clamp(char_center.z, obox.min.z, obox.max.z);

            Vector3 diff = Vector3Subtract(char_center, closest_point);
            float   len  = Vector3Length(diff);

            Vector3 hit_normal = { 0.0f, 1.0f, 0.0f };
            if (len > 0.0001f) {
                hit_normal = Vector3Scale(diff, 1.0f / len);
            }

            SurfaceType surface_type = classifySurfaceNormal(hit_normal);

            if (surface_type == SurfaceType::CEILING_SURF) {
                if (char_pos.y + height > obox.min.y && char_pos.y < obox.min.y) {
                    char_pos.y = obox.min.y - height;
                    if (character->getVerticalVelocity() > 0.0f) {
                        character->setVerticalVelocity(0.0f);
                    }
                    character->setPosition(char_pos);
                }
            } else if (surface_type == SurfaceType::WALL_SURF) {
                bool resolved = CollisionMath::resolveCylinderAABB(char_pos, radius, obox);
                if (resolved) {
                    character->setPosition(char_pos);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Main physics update — 4-step pipeline:
//   1. Gravity accumulation
//   2. Vertical integration  (pos.y += v_y * dt)
//   3. Overlap / Depenetration ejection  (2-iteration solver)
//   4. Ground snapping / isGrounded classification
// ---------------------------------------------------------------------------
std::vector<Vector3> PhysicsManager::updatePhysics(
    const std::vector<Character*>& characters,
    const std::vector<PhysicsObstacle>& obstacles,
    float dt)
{
    const float GRAVITY       = 9.81f;
    const float MAX_STEP      = 0.3f;
    const float SNAP_BAND     = 0.35f;
    const float COS_MAX_SLOPE = std::cos(45.0f * DEG2RAD);

    std::vector<Vector3> new_positions(characters.size());

    for (size_t i = 0; i < characters.size(); ++i) {
        Character* character = characters[i];
        assert(character != nullptr);
        new_positions[i] = character->getPosition();

        if (character->getStats().isDead()) {
            continue;
        }

        Vector3 pos = character->getPosition();

        // -----------------------------------------------------------------
        // STEP 1: Gravity
        // -----------------------------------------------------------------
        float v_y = character->getVerticalVelocity();
        if (!character->isGrounded()) {
            v_y -= GRAVITY * dt;
            character->setVerticalVelocity(v_y);
        }

        // -----------------------------------------------------------------
        // STEP 2 & 3: Sub-stepped Position integration + Depenetration
        //
        // Running multiple sub-steps to sweep the movement vector and prevent tunneling.
        // Running two passes per step ensures corners are resolved cleanly.
        // -----------------------------------------------------------------
        const float radius = character->getColliderRadius();
        const float height = character->getColliderHeight();

        Vector3 h_vel = character->getHorizontalVelocity();
        Vector3 h_movement = { h_vel.x * dt, 0.0f, h_vel.z * dt };
        float h_dist = Vector3Length(h_movement);
        int steps = std::ceil(h_dist / (radius * 0.5f));
        if (steps < 1) steps = 1;

        Vector3 step_h = Vector3Scale(h_movement, 1.0f / steps);
        float dt_step = dt / steps;

        for (int s = 0; s < steps; ++s) {
            float step_v = v_y * dt_step;
            
            pos.x += step_h.x;
            pos.y += step_v;
            pos.z += step_h.z;

            for (int iter = 0; iter < 2; ++iter) {
                for (const PhysicsObstacle& obs : obstacles) {
                BoundingBox obox = obs.getApproxBox();

                // ---- RAMP_SHAPE dispatch ----
                BoundingBox local_box = obs.getLocalBox();
                Vector3 local_pos = Vector3Transform(pos, obs.getWorldToLocal());

                // A ramp has three distinct contact zones:
                if (obs.getShape() == ObstacleShape::RAMP_SHAPE) {
                    // Explicitly ignore collision if character is completely beneath the base of the Half-Box
                    if (local_pos.y + height <= local_box.min.y) {
                        continue;
                    }

                    Vector3 local_clamped;
                    local_clamped.x = std::clamp(local_pos.x, local_box.min.x, local_box.max.x);
                    local_clamped.y = local_pos.y;
                    local_clamped.z = std::clamp(local_pos.z, local_box.min.z, local_box.max.z);

                    Vector3 world_clamped = Vector3Transform(local_clamped, obs.getLocalToWorld());
                    float slope_y = obs.getHeightAt(world_clamped);

                    if (slope_y <= pos.y + MAX_STEP) {
                        // Character can step up / is on top of slope. Skip XZ push.
                        continue;
                    }

                    if (obs.containsXZ(pos, 0.0f)) {
                        // Center is inside! They must have fallen onto the ramp, or glitched through the side.
                        // Snap them UP to the surface to prevent falling through the solid half-box!
                        pos.y = slope_y;
                        if (v_y < 0.0f) {
                            v_y = 0.0f;
                            character->setVerticalVelocity(0.0f);
                        }
                        continue;
                    }

                    // Otherwise, they are hitting the side or the tall back wall.
                    // Treat it as a solid vertical wall (Half Box) and push out in XZ.
                    CollisionMath::resolveCylinderAABB(local_pos, radius, local_box);
                    pos = Vector3Transform(local_pos, obs.getLocalToWorld());
                    continue;
                }

                // ---- BOX_SHAPE: normal-based dispatch in local space ----
                Vector3 local_center;
                local_center.x = local_pos.x;
                local_center.y = local_pos.y + height * 0.5f;
                local_center.z = local_pos.z;

                Vector3 local_closest;
                local_closest.x = std::clamp(local_center.x, local_box.min.x, local_box.max.x);
                local_closest.y = std::clamp(local_center.y, local_box.min.y, local_box.max.y);
                local_closest.z = std::clamp(local_center.z, local_box.min.z, local_box.max.z);

                Vector3 local_diff = Vector3Subtract(local_center, local_closest);
                float   len  = Vector3Length(local_diff);

                Vector3 hit_normal = { 0.0f, 1.0f, 0.0f };
                if (len > 0.0001f) {
                    hit_normal = Vector3Scale(local_diff, 1.0f / len);
                }

                if (hit_normal.y > 0.1f) {
                    if (local_box.max.y - local_pos.y > MAX_STEP) {
                        CollisionMath::resolveCylinderAABB(local_pos, radius, local_box);
                        pos = Vector3Transform(local_pos, obs.getLocalToWorld());
                    } else {
                        bool feet_below_top    = local_pos.y  < local_box.max.y;
                        bool head_above_bottom = local_pos.y + height > local_box.min.y;
                        bool approaching_from_below = local_pos.y < local_box.min.y; 
                        if (feet_below_top && head_above_bottom && approaching_from_below) {
                            local_pos.y = local_box.max.y;
                            pos = Vector3Transform(local_pos, obs.getLocalToWorld());
                        }
                    }
                } else if (hit_normal.y < -0.2f) {
                    if (local_pos.y + height > local_box.min.y && local_pos.y < local_box.min.y) {
                        local_pos.y = std::min(local_pos.y, local_box.min.y - height);
                        pos = Vector3Transform(local_pos, obs.getLocalToWorld());
                        if (v_y > 0.0f) {
                            v_y = 0.0f;
                            character->setVerticalVelocity(0.0f);
                        }
                    }
                } else {
                    CollisionMath::resolveCylinderAABB(local_pos, radius, local_box);
                    pos = Vector3Transform(local_pos, obs.getLocalToWorld());
                }
            }
        } // end solver iterations
        } // end sub-steps

        // -----------------------------------------------------------------
        // STEP 4: Ground snapping & isGrounded classification
        //
        // Scan all obstacles to find the highest valid supporting surface at or
        // below (pos.y + MAX_STEP). Rejects surfaces above that ceiling so a
        // character walking under an elevated ramp is not snapped up to it.
        //
        // Fix (Bug 1): ground_y is initialised to -infinity so the base world
        // floor at Y=0 is NOT the implicit fallback. Characters in open air
        // with no ground below them correctly become airborne instead of
        // sticking to an invisible floor.
        // -----------------------------------------------------------------
        bool    found_surface  = false;
        float   ground_y       = -std::numeric_limits<float>::infinity();
        Vector3 surface_normal = { 0.0f, 1.0f, 0.0f };

        for (const PhysicsObstacle& obs : obstacles) {
            if (!obs.containsXZ(pos, radius)) {
                continue;
            }

            float   candidate_y      = obs.getHeightAt(pos);
            Vector3 candidate_normal = obs.getNormal();

            // Reject surfaces that are too steep to stand on.
            if (candidate_normal.y < COS_MAX_SLOPE) {
                continue;
            }

            // Reject surfaces above the step-up ceiling.
            // This is the key gate that prevents an overhead ramp surface from
            // being selected as the floor when walking underneath it.
            if (candidate_y > pos.y + MAX_STEP) {
                continue;
            }

            if (candidate_y > ground_y) {
                ground_y       = candidate_y;
                surface_normal = candidate_normal;
                found_surface  = true;
            }
        }

        // Only grounded logic runs when a real surface was found below the character.
        // If found_surface == false there is nothing underfoot; gravity takes over.
        if (!found_surface) {
            // No obstacle surface detected — character is airborne.
            // Clamp at world origin to avoid falling to -infinity, but do not snap.
            // The world floor at Y=0 is handled as a hard floor only if pos.y < 0.
            if (pos.y < 0.0f) {
                pos.y = 0.0f;
                v_y   = 0.0f;
                character->setVerticalVelocity(0.0f);
                character->setGrounded(true);
            } else {
                character->setGrounded(false);
            }
            new_positions[i] = pos;
            continue;
        }

        bool is_walkable = (classifySurfaceNormal(surface_normal) == SurfaceType::GROUND_SURF);

        // --- Downhill snap: hold character to slope while descending ---
        // Fires only when a real surface is sampled (found_surface == true),
        // preventing ground_y=-inf or a stale 0.0 from dragging the character down.
        if (is_walkable && v_y <= 0.0f &&
            pos.y >= ground_y && (pos.y - ground_y) <= SNAP_BAND)
        {
            pos.y = ground_y;
            v_y   = 0.0f;
            character->setVerticalVelocity(0.0f);
            character->setGrounded(true);
            new_positions[i] = pos;
            continue;
        }

        // --- Standard upward step snap ---
        bool can_snap  = is_walkable;
        float step_diff = ground_y - pos.y;

        if (step_diff > MAX_STEP || step_diff < -MAX_STEP) {
            can_snap = false;
        }
        // Headroom check uses the live integrated pos, not character->getPosition().
        if (can_snap && step_diff > 0.001f) {
            if (!checkHeadroomClearance(pos, radius, height, ground_y, obstacles)) {
                can_snap = false;
            }
        }

        if (can_snap) {
            pos.y = ground_y;
            character->setVerticalVelocity(0.0f);
            character->setGrounded(true);
        } else {
            if (pos.y <= ground_y) {
                // Character has fallen to or below the detected surface — hard floor.
                pos.y = ground_y;
                character->setVerticalVelocity(0.0f);
                character->setGrounded(true);
            } else {
                // Character is airborne above the surface.
                character->setGrounded(false);
            }
        }

        new_positions[i] = pos;
    }

    resolveCharacterCollisions(characters, new_positions);
    return new_positions;
}

// ---------------------------------------------------------------------------
// Debug drawing — obstacles + character colliders
// ---------------------------------------------------------------------------
void PhysicsManager::drawDebug(
    const std::vector<Character*>& characters,
    const std::vector<PhysicsObstacle>& obstacles) const
{
    for (const PhysicsObstacle& obs : obstacles) {
        BoundingBox obox = obs.getApproxBox();
        DrawBoundingBox(obox, DARKBLUE);
        obs.draw();
    }

    for (const Character* character : characters) {
        assert(character != nullptr);
        if (character->getStats().isDead()) {
            continue;
        }

        BoundingBox char_box = character->getBoundingBox();
        DrawBoundingBox(char_box, GREEN);

        Vector3 pos    = character->getPosition();
        float   radius = character->getColliderRadius();
        float   height = character->getColliderHeight();

        Vector3 base_center = { pos.x, pos.y,          pos.z };
        Vector3 top_center  = { pos.x, pos.y + height, pos.z };

        DrawCylinderWires(base_center, radius, radius, height, 12, LIME);
        DrawCircle3D(base_center, radius, { 1.0f, 0.0f, 0.0f }, 90.0f, GREEN);
        DrawCircle3D(top_center,  radius, { 1.0f, 0.0f, 0.0f }, 90.0f, GREEN);
    }
}

// ---------------------------------------------------------------------------
// resolveGroundCollisions — legacy helper, delegates to updatePhysics pipeline.
// ---------------------------------------------------------------------------
void PhysicsManager::resolveGroundCollisions(
    const std::vector<Character*>& characters,
    const std::vector<PhysicsObstacle>& obstacles,
    float dt)
{
    updatePhysics(characters, obstacles, dt);
}
