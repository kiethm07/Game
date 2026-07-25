#include <GameManager/PhysicsManager.h>
#include <raymath.h>

void PhysicsManager::resolveCharacterCollisions(const std::vector<Character*>& characters) {
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

            Vector3 pos_a = char_a->getPosition();
            Vector3 pos_b = char_b->getPosition();
            float radius_a = char_a->getColliderRadius();
            float radius_b = char_b->getColliderRadius();

            bool resolved = CollisionMath::resolveCylinderCylinder(pos_a, radius_a, pos_b, radius_b);
            if (resolved) {
                char_a->setPosition(pos_a);
                char_b->setPosition(pos_b);
            }
        }
    }
}

bool PhysicsManager::checkHeadroomClearance(const Character* character, float target_y, const std::vector<WallObstacle>& walls, const Terrain& terrain) const {
    assert(character != nullptr);

    float radius = character->getColliderRadius();
    float height = character->getColliderHeight();
    Vector3 pos = character->getPosition();

    BoundingBox target_box;
    target_box.min = { pos.x - radius, target_y + 0.1f, pos.z - radius };
    target_box.max = { pos.x + radius, target_y + height, pos.z + radius };

    for (const WallObstacle& wall : walls) {
        BoundingBox wbox = wall.getBox();
        if (CheckCollisionBoxes(target_box, wbox)) {
            if (wbox.min.y >= pos.y + 0.1f) {
                return false;
            }
        }
    }

    for (const TerrainPlatform& platform : terrain.getPlatforms()) {
        BoundingBox pbox = platform.getBox();
        if (CheckCollisionBoxes(target_box, pbox)) {
            if (pbox.min.y >= pos.y + 0.1f) {
                return false;
            }
        }
    }

    return true;
}

SurfaceType PhysicsManager::classifySurfaceNormal(const Vector3& normal) const {
    if (normal.y > 0.5f) {
        return SurfaceType::Ground;
    }
    if (normal.y < -0.2f) {
        return SurfaceType::Ceiling;
    }
    return SurfaceType::Wall;
}

void PhysicsManager::resolveEnvironmentCollisions(const std::vector<Character*>& characters, const std::vector<WallObstacle>& walls, const Terrain& terrain) {
    for (Character* character : characters) {
        assert(character != nullptr);
        if (character->getStats().isDead()) {
            continue;
        }

        Vector3 char_pos = character->getPosition();
        float radius = character->getColliderRadius();
        float height = character->getColliderHeight();

        // 1. Resolve Wall & Ceiling AABB collisions
        for (const WallObstacle& wall : walls) {
            BoundingBox wbox = wall.getBox();
            Vector3 char_center = { char_pos.x, char_pos.y + height * 0.5f, char_pos.z };

            Vector3 closest_point;
            closest_point.x = std::clamp(char_center.x, wbox.min.x, wbox.max.x);
            closest_point.y = std::clamp(char_center.y, wbox.min.y, wbox.max.y);
            closest_point.z = std::clamp(char_center.z, wbox.min.z, wbox.max.z);

            Vector3 diff = Vector3Subtract(char_center, closest_point);
            float len = Vector3Length(diff);

            Vector3 hit_normal = { 0.0f, 1.0f, 0.0f };
            if (len > 0.0001f) {
                hit_normal = Vector3Scale(diff, 1.0f / len);
            }

            SurfaceType surface_type = classifySurfaceNormal(hit_normal);

            if (surface_type == SurfaceType::Ceiling) {
                if (char_pos.y + height > wbox.min.y && char_pos.y < wbox.min.y) {
                    char_pos.y = wbox.min.y - height;
                    if (character->getVerticalVelocity() > 0.0f) {
                        character->setVerticalVelocity(0.0f);
                    }
                    character->setPosition(char_pos);
                }
            } else if (surface_type == SurfaceType::Wall) {
                bool resolved = CollisionMath::resolveCylinderAABB(char_pos, radius, wbox);
                if (resolved) {
                    character->setPosition(char_pos);
                }
            }
        }

        // 2. Resolve Terrain Platform Ceilings
        for (const TerrainPlatform& platform : terrain.getPlatforms()) {
            BoundingBox pbox = platform.getBox();
            if (char_pos.x + radius > pbox.min.x && char_pos.x - radius < pbox.max.x &&
                char_pos.z + radius > pbox.min.z && char_pos.z - radius < pbox.max.z) {

                Vector3 ceiling_normal = { 0.0f, -1.0f, 0.0f };
                SurfaceType surface_type = classifySurfaceNormal(ceiling_normal);

                if (surface_type == SurfaceType::Ceiling) {
                    if (char_pos.y + height > pbox.min.y && char_pos.y < pbox.min.y) {
                        char_pos.y = pbox.min.y - height;
                        if (character->getVerticalVelocity() > 0.0f) {
                            character->setVerticalVelocity(0.0f);
                        }
                        character->setPosition(char_pos);
                    }
                }
            }
        }

        // 3. Resolve Ramp Overhead / Ceilings
        for (const TerrainRamp& ramp : terrain.getRamps()) {
            if (ramp.contains(char_pos)) {
                float ramp_y = ramp.getHeightAt(char_pos);
                Vector3 ramp_normal = ramp.getNormal();
                Vector3 overhead_normal = { -ramp_normal.x, -ramp_normal.y, -ramp_normal.z };

                SurfaceType surface_type = classifySurfaceNormal(overhead_normal);

                if (surface_type == SurfaceType::Ceiling && char_pos.y + height > ramp_y && char_pos.y < ramp_y) {
                    char_pos.y = ramp_y - height;
                    if (character->getVerticalVelocity() > 0.0f) {
                        character->setVerticalVelocity(0.0f);
                    }
                    character->setPosition(char_pos);
                }
            }
        }
    }
}

void PhysicsManager::updatePhysics(const std::vector<Character*>& characters, const Terrain& terrain, const std::vector<WallObstacle>& walls, float dt) {
    const float GRAVITY = 9.81f;
    const float MAX_STEP_HEIGHT = 0.3f;
    const float COS_MAX_SLOPE = std::cos(45.0f * DEG2RAD);

    for (Character* character : characters) {
        assert(character != nullptr);
        if (character->getStats().isDead()) {
            continue;
        }

        Vector3 pos = character->getPosition();

        // -------------------------------------------------------------
        // STEP 1: Apply gravity to velocity
        // -------------------------------------------------------------
        float v_y = character->getVerticalVelocity();
        if (!character->isGrounded()) {
            v_y -= GRAVITY * dt;
            character->setVerticalVelocity(v_y);
        }

        // -------------------------------------------------------------
        // STEP 2: Add velocity * deltaTime to position
        // -------------------------------------------------------------
        Vector3 hv = character->getHorizontalVelocity();
        pos.x += hv.x * dt;
        pos.z += hv.z * dt;
        pos.y += v_y * dt;

        // -------------------------------------------------------------
        // STEP 3: Run Overlap/Depenetration Ejection loop (with Upward Bias)
        // -------------------------------------------------------------
        float radius = character->getColliderRadius();
        float height = character->getColliderHeight();

        // 3a. Wall & Ceiling AABB Depenetration
        for (const WallObstacle& wall : walls) {
            BoundingBox wbox = wall.getBox();
            Vector3 char_center = { pos.x, pos.y + height * 0.5f, pos.z };

            Vector3 closest_point;
            closest_point.x = std::clamp(char_center.x, wbox.min.x, wbox.max.x);
            closest_point.y = std::clamp(char_center.y, wbox.min.y, wbox.max.y);
            closest_point.z = std::clamp(char_center.z, wbox.min.z, wbox.max.z);

            Vector3 diff = Vector3Subtract(char_center, closest_point);
            float len = Vector3Length(diff);

            Vector3 hit_normal = { 0.0f, 1.0f, 0.0f };
            if (len > 0.0001f) {
                hit_normal = Vector3Scale(diff, 1.0f / len);
            }

            // Upward Bias for Ground Ejection (REQUIREMENT 1)
            if (hit_normal.y > 0.1f) {
                // Force positive Y ejection (NEVER push downward)
                if (pos.y < wbox.max.y && pos.y + height > wbox.min.y) {
                    pos.y = std::max(pos.y, wbox.max.y);
                }
            } else if (hit_normal.y < -0.2f) {
                // Ceiling hitNormal: push Y downward only, zero upward velocity
                if (pos.y + height > wbox.min.y && pos.y < wbox.min.y) {
                    pos.y = std::min(pos.y, wbox.min.y - height);
                    if (v_y > 0.0f) {
                        v_y = 0.0f;
                        character->setVerticalVelocity(0.0f);
                    }
                }
            } else {
                // Wall hitNormal: depenetrate horizontally
                bool resolved = CollisionMath::resolveCylinderAABB(pos, radius, wbox);
                if (resolved) {
                    // Pos updated horizontally
                }
            }
        }

        // 3b. Terrain Platform Ceilings & Floors
        for (const TerrainPlatform& platform : terrain.getPlatforms()) {
            BoundingBox pbox = platform.getBox();
            if (pos.x + radius > pbox.min.x && pos.x - radius < pbox.max.x &&
                pos.z + radius > pbox.min.z && pos.z - radius < pbox.max.z) {

                // Ceiling hit check
                if (pos.y + height > pbox.min.y && pos.y < pbox.min.y) {
                    pos.y = std::min(pos.y, pbox.min.y - height);
                    if (v_y > 0.0f) {
                        v_y = 0.0f;
                        character->setVerticalVelocity(0.0f);
                    }
                }
            }
        }

        // -------------------------------------------------------------
        // STEP 4: Final Ground Snapping / Y-Axis Override & isGrounded checks
        // -------------------------------------------------------------
        float ground_y = terrain.getHeightAt(pos);
        float step_diff = ground_y - pos.y;

        Vector3 surface_normal = { 0.0f, 1.0f, 0.0f };
        SurfaceType surface_type = SurfaceType::Ground;

        for (const TerrainRamp& ramp : terrain.getRamps()) {
            if (ramp.contains(pos)) {
                surface_normal = ramp.getNormal();
                surface_type = classifySurfaceNormal(surface_normal);
                break;
            }
        }

        bool is_slope_walkable = (surface_type == SurfaceType::Ground);
        bool can_snap = true;

        // Snapping condition: only snap if player is within 0.3m of floor surface
        if (step_diff > MAX_STEP_HEIGHT || step_diff < -MAX_STEP_HEIGHT) {
            can_snap = false;
        }

        if (!is_slope_walkable) {
            can_snap = false;
        }

        if (can_snap && step_diff > 0.001f) {
            bool headroom_ok = checkHeadroomClearance(character, ground_y, walls, terrain);
            if (!headroom_ok) {
                can_snap = false;
            }
        }

        if (can_snap) {
            // Y-Axis Override: snap exact position.y to ground surface mesh
            pos.y = ground_y;
            character->setVerticalVelocity(0.0f);
            character->setGrounded(true);
        } else {
            if (pos.y <= ground_y) {
                pos.y = ground_y;
                character->setVerticalVelocity(0.0f);
                character->setGrounded(true);
            } else {
                character->setGrounded(false);
            }
        }

        character->setPosition(pos);
    }

    // Resolve Character-to-Character push-out
    resolveCharacterCollisions(characters);
}

void PhysicsManager::drawDebug(const std::vector<Character*>& characters, const std::vector<WallObstacle>& walls) const {
    // 1. Draw Wall Bounding Boxes & Wireframes
    for (const WallObstacle& wall : walls) {
        BoundingBox box = wall.getBox();
        DrawBoundingBox(box, DARKBLUE);

        Vector3 size = Vector3Subtract(box.max, box.min);
        Vector3 center = Vector3Add(box.min, Vector3Scale(size, 0.5f));

        DrawCube(center, size.x, size.y, size.z, wall.getColor());
        DrawCubeWires(center, size.x, size.y, size.z, BLACK);
    }

    // 2. Draw Character Physics Bounding Boxes & Cylinder Colliders
    for (const Character* character : characters) {
        assert(character != nullptr);
        if (character->getStats().isDead()) {
            continue;
        }

        // Draw character's Axis-Aligned Bounding Box (AABB)
        BoundingBox char_box = character->getBoundingBox();
        DrawBoundingBox(char_box, GREEN);

        // Draw character's cylinder collider wireframe
        Vector3 pos = character->getPosition();
        float radius = character->getColliderRadius();
        float height = character->getColliderHeight();

        Vector3 base_center = { pos.x, pos.y, pos.z };
        Vector3 top_center = { pos.x, pos.y + height, pos.z };

        DrawCylinderWires(base_center, radius, radius, height, 12, LIME);
        DrawCircle3D(base_center, radius, { 1.0f, 0.0f, 0.0f }, 90.0f, GREEN);
        DrawCircle3D(top_center, radius, { 1.0f, 0.0f, 0.0f }, 90.0f, GREEN);
    }
}
