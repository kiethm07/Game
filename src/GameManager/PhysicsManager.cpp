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
    if (normal.y >  0.4f) return SurfaceType::GROUND_SURF;
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

            bool center_inside_xz = (char_pos.x >= obox.min.x && char_pos.x <= obox.max.x &&
                                     char_pos.z >= obox.min.z && char_pos.z <= obox.max.z);

            SurfaceType surface_type = classifySurfaceNormal(hit_normal);
            if (!center_inside_xz && surface_type == SurfaceType::CEILING_SURF) {
                surface_type = SurfaceType::WALL_SURF;
            }

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
    const float COS_MAX_SLOPE = std::cos(60.0f * DEG2RAD);

    // How far past a surface's edge ground support still reaches, as a fraction
    // of the collider radius, so a character whose centre has just cleared a
    // ledge does not drop the instant it does.
    //
    // Must stay strictly BELOW 1.0. resolveCylinderAABB parks a blocked
    // character at exactly `radius` from a wall's footprint, so probing at the
    // full radius leaves anyone pressed against a wall permanently on the
    // boundary of that same wall's "am I standing on this" test — the obstacle
    // ejects them and offers itself as floor in the same frame. Against a ramp
    // that reads as an invisible slope running alongside the real one, climbing
    // as they walk, which is what this constant exists to prevent.
    const float GROUND_PROBE_REACH = 0.5f;

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

                    // 1. Ceiling collision (Headbumping on ramp bottom)
                    // BUG FIX: Must check containsXZ so we don't bump our head on another ramp's infinite horizontal plane!
                    if (obs.containsXZ(pos, radius + 0.05f) && v_y > 0.0f && local_pos.y < local_box.min.y && local_pos.y + height > local_box.min.y) {
                        bool was_below = (local_pos.y + height - step_v) <= local_box.min.y;
                        if (was_below) {
                            local_pos.y = local_box.min.y - height - 0.01f;
                            pos = Vector3Transform(local_pos, obs.getLocalToWorld());
                            v_y = 0.0f;
                            character->setVerticalVelocity(0.0f);
                            continue;
                        }
                    }

                    // 2. Ramp slope surface
                    if (obs.containsXZ(pos, radius + 0.05f)) {
                        float slope_y = obs.getHeightAt(pos);

                        if (pos.y >= slope_y) {
                            continue; // Completely above the ramp! Skip XZ wall collision.
                        }

                        if (pos.y >= slope_y - MAX_STEP) {
                            continue; // Cleared the solid wall part of the ramp! Skip XZ wall collision.
                        }
                    }

                    // 3. Solid vertical side of ramp base
                    CollisionMath::resolveCylinderAABB(local_pos, radius, local_box);
                    pos = Vector3Transform(local_pos, obs.getLocalToWorld());
                    continue;
                }

                // ---- BOX_SHAPE: geometry-based dispatch in local space ----
                const float local_head_y = local_pos.y + height;

                // 1. Check vertical overlap between character cylinder and box
                if (local_pos.y >= local_box.max.y || local_head_y <= local_box.min.y) {
                    continue;
                }

                // 2. Are we landing on this box?
                // If our feet are just slightly below the top (within MAX_STEP),
                // we treat this as a floor. We SKIP XZ collision to let Step 4 (Gravity/Snapping) handle it!
                float depth_floor = local_box.max.y - local_pos.y;
                if (depth_floor > 0.0f && depth_floor <= MAX_STEP && v_y <= 0.0f) {
                    continue; // Skip Step 3! Let Step 4 perfectly snap us down/up to the floor!
                }

                // Calculate XZ distance to see if we even intersect the footprint
                float dx = 0.0f, dz = 0.0f;
                if (local_pos.x < local_box.min.x) dx = local_box.min.x - local_pos.x;
                else if (local_pos.x > local_box.max.x) dx = local_pos.x - local_box.max.x;

                if (local_pos.z < local_box.min.z) dz = local_box.min.z - local_pos.z;
                else if (local_pos.z > local_box.max.z) dz = local_pos.z - local_box.max.z;

                float dist_xz = sqrtf(dx*dx + dz*dz);
                
                if (dist_xz >= radius) {
                    continue; // Completely outside the cylinder's XZ radius
                }

                // 3. Ceiling collision (Headbumping):
                // If moving upward and the physics cylinder penetrates the bottom face of the box, BUMP HEAD!
                bool was_below_ceiling = (local_head_y - step_v) <= local_box.min.y;
                if (v_y > 0.0f && local_pos.y < local_box.min.y && local_head_y > local_box.min.y && was_below_ceiling) {
                    local_pos.y = local_box.min.y - height - 0.01f;
                    pos = Vector3Transform(local_pos, obs.getLocalToWorld());
                    v_y = 0.0f;
                    character->setVerticalVelocity(0.0f);
                    continue;
                }

                // 4. Otherwise, it is a WALL. Push out purely in XZ!
                // Capsule Bottom prevents sudden shoves off cliffs when falling
                float effective_radius = radius;
                if (v_y < 0.0f && depth_floor > 0.0f && depth_floor < radius) {
                    effective_radius = sqrtf(2.0f * radius * depth_floor - depth_floor * depth_floor);
                }

                if (dist_xz < effective_radius) {
                    bool resolved = CollisionMath::resolveCylinderAABB(local_pos, effective_radius, local_box);
                    if (resolved) {
                        pos = Vector3Transform(local_pos, obs.getLocalToWorld());
                    }
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
            if (!obs.containsXZ(pos, radius * GROUND_PROBE_REACH)) {
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
            // No obstacle surface detected. The world floor at Y=0 is the only
            // thing left underfoot, so it has to be a surface a character can
            // REST on, not merely a backstop that catches them once they are
            // already below it.
            //
            // The comparison is <=, not <. At exactly y == 0 a strict < reports
            // airborne, gravity pulls the character a hair under, the clamp puts
            // them back on 0 and marks them grounded, and the next frame reports
            // airborne again — is_grounded then alternates every single frame.
            // Harmless while nothing consumed it; once the animation layer
            // started picking a clip from it, standing on open ground flickered
            // between the jump and locomotion clips at 30Hz.
            //
            // Characters genuinely in mid-air still have pos.y > 0 and fall
            // normally, so this does not reintroduce the invisible floor that
            // ground_y = -infinity was written to avoid.
            if (pos.y <= 0.0f) {
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
        if (is_walkable && v_y <= 0.0f && character->isGrounded() &&
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

        // Never step-snap a character that is airborne (jumping or falling).
        // This fixes BUG 2: Snapping up through a ceiling after a headbump!
        if (!character->isGrounded()) {
            can_snap = false;
        }

        // Never snap a character that is moving upward.
        if (v_y > 0.0f) {
            can_snap = false;
        }

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
            // BUG 1 (Ramp snap when jump): The `&& v_y <= 0.0f` ensures that 
            // even if the ramp is steeper than our jump arc, we don't snap down to it!
            if (pos.y <= ground_y && v_y <= 0.0f) {
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
