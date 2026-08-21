#include "States/GamePlayState.h"
#include "raylib.h"
#include "raymath.h"
#include <Core/Game.h>
#include <Level/SpawnGround.h>
#include <Physics/CollisionMeshLoader.h>
#include <Stealth/Sensor.h>
#include <cassert>
#include <cmath>

// The level this state loads comes from Campaign, which Game owns and hands in
// by reference. It used to be a constant here, because StateAction carries no
// payload and there was nowhere for a level choice to come from; the answer was
// to give the choice a home of its own rather than a seat on the action.
//
// See include/Level/CampaignManifest.h for the phase table.

GameplayState::GameplayState(const InputManager &input_manager, AssetManager &asset_manager,
                             SoundController &sound_controller, Campaign &campaign)
    : input_manager(input_manager), asset_manager(asset_manager),
      sound_controller(sound_controller), campaign(campaign) {
  camera_controller = std::make_unique<CameraController>();

  // The world, authored in Blender and exported by tools/export_level.py.
  // Everything below reads from `level` rather than from literals: geometry,
  // spawns, and the extent the shadow cascades are framed against.
  level = LevelLoader::load(campaign.currentLevelPath());
  if (!level.valid) {
    // Deliberately not fatal. The player still lands on PhysicsManager's y=0
    // floor clamp, so the game comes up empty and obviously broken with the
    // reason already logged, rather than crashing on startup and taking the
    // reason with it.
    // Named by phase, not just by path. With three phases in a run this is
    // something a player can now walk into mid-run rather than something a
    // developer sees seconds after editing a constant, so the message has to
    // say which phase broke.
    TraceLog(LOG_ERROR,
             "GameplayState: phase %d/%d ('%s') failed to load from '%s'; "
             "starting with an empty world.",
             (int)campaign.index() + 1, (int)campaign.count(),
             campaign.currentName(), campaign.currentLevelPath());
  }

  // Loaded here rather than in LevelLoader so a Level stays parseable without a
  // window, and owned here rather than by the renderer because this is physics
  // data that nothing draws. A level without one collides against its proxies
  // alone, which is the normal case for greybox and forest.
  if (!level.collisionMeshPath.empty()) {
    if (!CollisionMeshLoader::load(level.collisionMeshPath, collision_mesh)) {
      TraceLog(LOG_ERROR,
               "GameplayState: level '%s' declares a collision mesh that could "
               "not be loaded. Its terrain and buildings will not be solid.",
               level.name.c_str());
    }
  }

  player = std::make_unique<Player>(input_manager);

  // Load test SFX
  asset_manager.loadSound(AssetID::SFX_COIN, ASSET_DIR "/audio/coin.wav");
  asset_manager.loadSound(AssetID::SFX_HIT, ASSET_DIR "/audio/hit.mp3");
  asset_manager.loadSound(AssetID::SFX_DEFLECT_1, ASSET_DIR "/audio/deflect_1.MP3");
  asset_manager.loadSound(AssetID::SFX_DEFLECT_2, ASSET_DIR "/audio/Deflect_2.MP3");
  asset_manager.loadSound(AssetID::SFX_DEFLECT_NPC, ASSET_DIR "/audio/deflect_NPC.MP3");
  asset_manager.loadSound(AssetID::SFX_DEATHBLOW, ASSET_DIR "/audio/deflect_end.mp3");
  player->setPosition(level.playerSpawn.position);
  player->setRotation({0.0f, level.playerSpawn.yaw, 0.0f});

  // What the previous phase handed over. addMoney rather than a setter because
  // a freshly built Player's money is already 0, so adding IS restoring — and
  // health is absent from the carry on purpose: the Stats(1000, 100, 15) this
  // Player was constructed with a few lines up is what "restored at a seam"
  // means. On the first phase of a run the carry is empty and all of this is a
  // no-op.
  const PhaseCarry &carry = campaign.getCarry();
  player->addMoney(carry.money);
  player->restoreItemCounts(carry.itemCounts);

  // Enemies. `collision_mesh` is fully built above, which is what lets a spawn
  // that omitted its height be resolved here rather than guessed at.
  //
  // No setRotation after construction any more: Enemy's constructor takes the
  // whole spawn and sets position, rotation and the post it returns to together.
  // Setting rotation from out here is what let an authored facing be forgotten
  // the first time an enemy de-aggroed.
  int dropped = 0;
  for (const EnemySpawn &spawn : level.enemySpawns) {
    EnemySpawn placed = spawn;
    if (!placed.hasExplicitY) {
      float ground = 0.0f;
      if (!SpawnGround::highestUnder(collision_mesh, level.obstacles,
                                     level.bounds, placed.at.position.x,
                                     placed.at.position.z, ground)) {
        // Dropped rather than kept, because there is no authored height to
        // keep -- only a default-constructed 0. An enemy at y=0 in a level
        // whose terrain sits at -30 either falls forever or stands in mid-air,
        // which is the "looks playable while silently wrong" failure
        // manufactured out of nothing.
        TraceLog(LOG_WARNING,
                 "GameplayState: spawn %d (%s at x=%.1f z=%.1f) has no ground "
                 "under it and was dropped. It is over a hole or outside the "
                 "map; give it an explicit \"y\" if it is meant to be in the "
                 "air.",
                 dropped + (int)enemies.size(), enemyTypeName(placed.type),
                 placed.at.position.x, placed.at.position.z);
        ++dropped;
        continue;
      }
      placed.at.position.y = ground;
    }
    enemies.push_back(EnemyFactory::createEnemy(placed));
  }
  if (dropped > 0) {
    TraceLog(LOG_WARNING,
             "GameplayState: %d spawn(s) dropped for having no ground; %d "
             "enemies placed.",
             dropped, (int)enemies.size());
  }

  // --- NAVMESH SETUP ---
  // Obstacles must be complete before build(): the navmesh is baked once, here,
  // and there is no runtime rebuild path. The floor is one of them now — it
  // used to be a 200x200 plate fabricated on the spot for Recast alone, which
  // meant the floor the AI walked on, the floor the renderer drew, and the
  // floor physics clamped to were three separate things that could disagree.
  // The collision mesh first, where there is one: it carries the ground, and
  // since the castle's terrain stopped being boxes it is the only floor Recast
  // would ever see. A level with no mesh still bakes from its proxies alone.
  if (!collision_mesh.isEmpty()) {
    nav_builder.addCollisionMesh(collision_mesh);
  }
  for (const auto &obs : level.obstacles) {
    nav_builder.addObstacle(obs);
  }
  if (!nav_builder.build()) {
    // Silently discarding this used to be survivable when the geometry was a
    // literal in this file. Now that it comes from a level file, a bake that
    // fails is an authoring problem the author needs told about — otherwise it
    // presents as enemies that simply never move.
    TraceLog(LOG_ERROR,
             "GameplayState: navmesh bake failed for level '%s'. Enemies will "
             "not path. Check that the level has a floor proxy and that gaps "
             "are wider than the 0.6m agent radius.",
             level.name.c_str());
  } else {
    TraceLog(LOG_INFO,
             "GameplayState: navmesh baked from %d obstacles and %d collision "
             "mesh triangles",
             (int)level.obstacles.size(), collision_mesh.getTriangleCount());
  }
  nav_query.init(nav_builder.getNavMesh());

  renderer = std::make_unique<GameRenderer>(asset_manager, level);
}

PhaseCarry GameplayState::snapshotCarry() const {
  PhaseCarry carry;
  carry.money = player->getMoney();

  const auto &inventory = player->getInventory();
  carry.itemCounts.reserve(inventory.size());
  for (const auto &item : inventory) {
    carry.itemCounts.push_back(item->getCount());
  }

  // Uncollected MoneyDrops still lying on the ground are deliberately not
  // swept up into this. Walking out of a phase and leaving coins behind is
  // what makes leaving a decision rather than a formality.
  return carry;
}

void GameplayState::enter() {
  // Load local menu-only graphics or titles here
}

StateAction GameplayState::update(float dt) {
  // 1. Tick entities through the shared polymorphic update path. Each reads
  // input/AI internally and shifts its own position safely.
  Vector3 player_pos = player->getPosition();

  std::vector<Character *> active_characters;
  active_characters.reserve(1 + enemies.size());

  active_characters.push_back(player.get());
  for (auto &enemy : enemies) {
    if (enemy->isModelUnloaded()) continue;
    active_characters.push_back(enemy.get());
  }

  const UpdateContext ctx{dt, camera_controller->getCameraForward(),
                          camera_controller->getCameraRight(), player_pos,
                          &asset_manager, &nav_query, &level.obstacles,
                          &smoke_clouds, locked_target, &active_characters};

  // 1.5. Evaluate Stealth before AI update so AI can react in the same frame
  stealth_manager.update(active_characters, player.get(), level.obstacles,
                         collision_mesh.isEmpty() ? nullptr : &collision_mesh,
                         smoke_clouds, dt);

  player->update(ctx);
  for (auto &enemy : enemies) {
    if (enemy->isModelUnloaded()) continue;

    enemy->update(ctx);
    
    // Check for loot drops and handle decaying
    if (enemy->getStats().isDead()) {
        if (!enemy->hasDroppedLoot()) {
            enemy->setDroppedLoot(true);
            MoneyDrop md;
            md.position = enemy->getPosition();
            md.amount = 10 + rand() % 15;
            md.bob_timer = 0.0f;
            money_drops.push_back(md);
        }
        
        if (enemy->isKilledByStealth()) {
            enemy->addDissolveTimer(dt);
        }
    }
  }


  // Check for money pickups
  for (int i = (int)money_drops.size() - 1; i >= 0; --i) {
      money_drops[i].bob_timer += dt;
      if (Vector3DistanceSqr(player_pos, money_drops[i].position) < 2.0f * 2.0f) {
          player->addMoney(money_drops[i].amount);
          sound_controller.playSFX(AssetID::SFX_COIN);
          money_drops[i] = money_drops.back();
          money_drops.pop_back();
      }
  }

  // 2. Resolve Physics Pipeline (4-Step: Gravity -> Integration -> Ejection
  // Loop -> Ground Snap)
  std::vector<Vector3> new_positions =
      physics_manager.updatePhysics(active_characters, level.obstacles,
                                    collision_mesh.isEmpty() ? nullptr : &collision_mesh, dt);
  for (size_t i = 0; i < active_characters.size(); ++i) {
    active_characters[i]->setPosition(new_positions[i]);
  }

  // 3. Resolve Combat
  combat_manager.update(active_characters, &particle_manager, &sound_controller);

  // Shares segmentBlocked with the stealth sensors rather than keeping its own
  // copy of the test. The old copy walked the obstacle list only, so once the
  // castle's walls became mesh triangles it would have held lock-on through
  // solid stone.
  auto checkLineOfSight = [&](Vector3 start, Vector3 end) {
    return !segmentBlocked(start, end, level.obstacles,
                           collision_mesh.isEmpty() ? nullptr : &collision_mesh);
  };

  // Helper to get chest height position for raycasting
  auto getChestPos = [](Character* c) {
    Vector3 pos = c->getPosition();
    pos.y += c->getColliderHeight() * 0.8f;
    return pos;
  };

  // Lock-on target validation
  if (locked_target) {
    bool is_visible = checkLineOfSight(getChestPos(player.get()), getChestPos(locked_target));
    if (locked_target->getStats().isDead() || 
        Vector3Distance(player->getPosition(), locked_target->getPosition()) > 20.0f ||
        !is_visible) {
      locked_target = nullptr;
    }
  }

  if (input_manager.isActionPressed(GameAction::LockOn)) {
    if (locked_target) {
      locked_target = nullptr;
    } else {
      float best_score = 10000.0f;
      Character* best_target = nullptr;
      Vector3 cam_pos = camera_controller->getCamera().position;
      Vector3 cam_fwd = camera_controller->getCameraForward();

      for (const auto& enemy : enemies) {
        if (enemy->isModelUnloaded()) continue;

        if (!enemy->getStats().isDead()) {
          float dist_to_player = Vector3Distance(player->getPosition(), enemy->getPosition());
          Vector3 to_enemy = Vector3Subtract(enemy->getPosition(), cam_pos);
          float dist_to_cam = Vector3Length(to_enemy);
          
          if (dist_to_cam < 25.0f) { // Must be within a certain distance from camera
            Vector3 dir = Vector3Scale(to_enemy, 1.0f / dist_to_cam);
            float dot = Vector3DotProduct(cam_fwd, dir);
            
            if (dot > 0.0f) { 
              float angle = acosf(dot) * RAD2DEG;
              if (angle < 45.0f) { // Must be within a 45 degree arc from camera center
                // Score heavily penalizes being off-center, then considers distance
                float score = angle * 2.0f + dist_to_player;
                
                if (score < best_score) {
                  bool is_visible = checkLineOfSight(getChestPos(player.get()), getChestPos(enemy.get()));
                  if (is_visible) {
                    best_score = score;
                    best_target = enemy.get();
                  }
                }
              }
            }
          }
        }
      }
      locked_target = best_target;
    }
  }

  // 4. Update the camera tracking matrix using that position. Built here the
  // same way the UpdateContext above is, so the camera never reaches back into
  // the player for it — and so that the framing, which is a decision about the
  // world rather than about the camera, is made where the world is known.
  // The deathblow shot lasts exactly as long as the swing does — the animation
  // is what the camera is framing, so it is what decides when to let go. The
  // victim is dropped at the same moment, which is what stops a stale pointer
  // from ever reaching the camera.
  if (!player->isExecuting())
    deathblow_victim = nullptr;

  CameraFrame shot;
  shot.target = player->getPosition();
  shot.look = input_manager.getRawMouseDelta();
  shot.dt = dt;
  shot.targetYaw = player->getRotation().y;
  shot.framing =
      player->isDashing() ? CameraFraming::Wide : CameraFraming::Close;
  if (deathblow_victim) {
    shot.shot = CameraShot::Deathblow;
    shot.focus = deathblow_victim->getPosition();
  } else if (locked_target) {
    shot.shot = CameraShot::LockOn;
    shot.focus = locked_target->getPosition();
  }
  shot.obstacles = &level.obstacles;
  shot.collision_mesh = collision_mesh.isEmpty() ? nullptr : &collision_mesh;
  camera_controller->update(shot);

  // Debug affordance, not a game action, so it stays off InputManager's
  // GameAction enum. Shows the raw depth map so the light frustum can be
  // checked against where the arena actually is.
  if (IsKeyPressed(KEY_F1)) {
    show_shadow_map = !show_shadow_map;
  }

  // F2: collision proxies over the level mesh. The check that the Blender
  // export lined up — see GameRenderer::setDebugOverlay.
  if (IsKeyPressed(KEY_F2)) {
    renderer->setDebugOverlay(!renderer->debugOverlayEnabled());
  }

  // F3: cycle what the depth pass renders, for measuring its cost against the
  // fragment-side cost. The stats readout comes up with the F2 overlay.
  if (IsKeyPressed(KEY_F3)) {
    renderer->cycleShadowMode();
  }

  if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE)) {
    return StateAction::ChangeToMenu;
  }

  // L: leave this phase for the next one.
  //
  // A debug affordance in the same sense F1-F3 are, so it stays off
  // InputManager's GameAction enum for the reason given at the top of that
  // block -- and for one more: it is standing in for a trigger that will not
  // be a keypress at all. Once phases carry an exit marker this same body
  // fires from a proximity test, and a GameAction binding would be dead weight.
  //
  // The carry is snapshotted here rather than in exit() so that taking it and
  // deciding to leave are one statement. exit() also runs on quit-to-menu and
  // on shutdown, where writing a carry would be wrong, and the ordering
  // against Campaign::restart() would then have to be reasoned about.
  if (IsKeyPressed(KEY_L)) {
    campaign.setCarry(snapshotCarry());
    return StateAction::RequestNextPhase;
  }

  // F4: the player's position as a line to paste into
  // assets/levels/<phase>/enemies.json. Same debug-affordance convention as
  // F1-F3 above -- off InputManager's GameAction enum.
  //
  // Two decimals rather than the five the exporter writes: those exist for a
  // machine round-trip, and this number gets retyped by a person for whom a
  // centimetre of spawn placement has never mattered. The trailing comma is
  // there because the overwhelmingly common paste target is the middle of a
  // list, and deleting a comma beats remembering to add one.
  //
  // `y` goes on a second line rather than into the paste, because leaving it
  // out is the normal case -- the loader snaps to the ground under (x, z) --
  // and JSON has nowhere to put a commented-out field.
  if (IsKeyPressed(KEY_F4)) {
    const Vector3 p = player->getPosition();
    float yaw = player->getRotation().y;
    while (yaw <= -180.0f) yaw += 360.0f;
    while (yaw > 180.0f) yaw -= 360.0f;

    spawn_line = TextFormat(
        "{ \"type\": \"%s\", \"x\": %.2f, \"z\": %.2f, \"yaw\": %.1f },",
        enemyTypeName(debug_spawn_type), p.x, p.z, yaw);
    spawn_line_timer = 8.0f;
    TraceLog(LOG_INFO, "%s", spawn_line.c_str());
    TraceLog(LOG_INFO,
             "  (you are at y=%.2f -- add \"y\": %.2f to pin the height "
             "instead of snapping to the ground)",
             p.y, p.y);
  }

  // SHIFT+F4 cycles which type F4 writes, so the readout stays correct the day
  // a second type exists rather than needing to be remembered.
  if (IsKeyPressed(KEY_F4) && IsKeyDown(KEY_LEFT_SHIFT)) {
    debug_spawn_type = static_cast<EnemyType>(
        (static_cast<int>(debug_spawn_type) + 1) % kEnemyTypeCount);
  }

  // F5: rebuild this phase, re-reading level.json and enemies.json. The other
  // half of the authoring loop -- edit the file, press this, see it.
  if (IsKeyPressed(KEY_F5)) {
    return StateAction::RequestReloadPhase;
  }

  if (spawn_line_timer > 0.0f) spawn_line_timer -= dt;

  if (smoke_cooldown_timer > 0.0f) {
      smoke_cooldown_timer -= dt;
  }

  // Process smoke clouds spawned by items (e.g. Smoke Bomb)
  std::vector<SmokeCloud> new_clouds = player->takePendingSmokeClouds();
  for (const auto& sc : new_clouds) {
      smoke_clouds.push_back(sc);
      particle_manager.emitVisualSmoke(sc.position, sc.radius, sc.life);
  }

  // Debug smoke spawning on enemy
  if (IsKeyPressed(KEY_P) && !enemies.empty() && smoke_cooldown_timer <= 0.0f) {
      SmokeCloud sc;
      sc.position = enemies[0]->getPosition();
      sc.radius = 3.5f;
      sc.life = 6.0f;
      sc.owner = enemies[0].get();
      smoke_clouds.push_back(sc);
      particle_manager.emitVisualSmoke(sc.position, sc.radius, sc.life);
      smoke_cooldown_timer = 8.0f; // 8 seconds cooldown
  }

  // Update smoke data lifetimes
  for (int i = (int)smoke_clouds.size() - 1; i >= 0; --i) {
      smoke_clouds[i].life -= dt;
      if (smoke_clouds[i].life <= 0.0f) {
          smoke_clouds[i] = smoke_clouds.back();
          smoke_clouds.pop_back();
      }
  }

  particle_manager.update(dt);

  if (takedown_text_timer > 0.0f) {
    takedown_text_timer -= dt;
  }

  // --- Pending Aerial Takedown Logic ---
  if (pending_aerial_target) {
    if (pending_aerial_target->getStats().isDead()) {
      pending_aerial_target = nullptr; // Abort if target somehow died
    } else {
      Vector3 p_pos = player->getPosition();
      Vector3 e_pos = pending_aerial_target->getPosition();

      // Smoothly lerp X and Z to the target while falling
      float lerp_factor = std::fmin(10.0f * dt, 1.0f);
      float new_x = p_pos.x + (e_pos.x - p_pos.x) * lerp_factor;
      float new_z = p_pos.z + (e_pos.z - p_pos.z) * lerp_factor;
      player->setPosition({new_x, p_pos.y, new_z});

      // Face the enemy while falling
      Vector3 to_enemy = Vector3Subtract(e_pos, p_pos);
      if (to_enemy.x * to_enemy.x + to_enemy.z * to_enemy.z > 0.001f) {
          float target_yaw = std::atan2(to_enemy.x, to_enemy.z) * RAD2DEG;
          player->setRotation({0.0f, target_yaw, 0.0f});
      }

      // If we reach the threshold Y or hit the ground
      if (p_pos.y - e_pos.y < 0.2f || player->isGrounded()) {
        takedown_type_str = "AERIAL TAKEDOWN";
        
        // Snap position 1.2 units away so they aren't inside each other (prevents physics ejection)
        Vector3 dir = to_enemy;
        dir.y = 0.0f;
        if (Vector3LengthSqr(dir) > 0.001f) {
            dir = Vector3Normalize(dir);
            player->setPosition({e_pos.x - dir.x * 1.2f, e_pos.y, e_pos.z - dir.z * 1.2f});
        } else {
            player->setPosition({e_pos.x, e_pos.y, e_pos.z + 1.2f});
        }
        
        player->setVerticalVelocity(0.0f);
        // Player keeps the rotation looking at the enemy
        
        // Force the enemy to face away from the player (turn their back to the player)
        // so the execution animation doesn't look like they are face-to-face
        pending_aerial_target->setRotation(player->getRotation());

        // Let the hitbox apply the damage and blood in sync with the animation
        if (Enemy* e = dynamic_cast<Enemy*>(pending_aerial_target)) {
            e->getCombatComponent().setBeingExecuted();
            e->setKilledByStealth(true);
            e->setDecayType(DecayType::PETAL_DECAY);
        }
        sound_controller.playSFX(AssetID::SFX_DEATHBLOW);
        player->performTakedown();
        deathblow_victim = pending_aerial_target;
        takedown_text_timer = 2.0f;

        pending_aerial_target = nullptr;
      }
    }
  }

  // Takedown logic
  if (!pending_aerial_target && !player->isExecuting() &&
      input_manager.isActionPressed(GameAction::Takedown)) {
    for (auto &enemy_ptr : enemies) {
      Enemy *enemy = enemy_ptr.get();
      if (!enemy->getStats().isDead()) {
        Vector3 p_pos = player->getPosition();
        Vector3 e_pos = enemy->getPosition();

        Vector2 p2 = {p_pos.x, p_pos.z};
        Vector2 e2 = {e_pos.x, e_pos.z};
        float horiz_dist = Vector2Distance(p2, e2);
        float vert_dist = p_pos.y - e_pos.y;

        // Tighter vertical check for grounded takedowns so you don't do
        // grounded takedowns from a ledge
        bool is_normal_range =
            (horiz_dist < 2.0f && std::abs(vert_dist) < 0.5f);
        bool is_aerial_range = (horiz_dist < 2.5f && vert_dist >= 1.5f &&
                                vert_dist < 10.0f && !player->isGrounded());

        if (is_normal_range || is_aerial_range) {
          // Raycast check to prevent takedowns through walls
          bool is_blocked = false;
          Vector3 p_head = {
              p_pos.x, p_pos.y + player->getColliderHeight() * 0.8f, p_pos.z};
          Vector3 e_head = {
              e_pos.x, e_pos.y + enemy->getColliderHeight() * 0.8f, e_pos.z};

          for (const auto &obs : level.obstacles) {
            Vector3 local_obs = Vector3Transform(p_head, obs.getWorldToLocal());
            Vector3 local_tgt = Vector3Transform(e_head, obs.getWorldToLocal());

            Ray local_ray;
            local_ray.position = local_obs;
            local_ray.direction =
                Vector3Normalize(Vector3Subtract(local_tgt, local_obs));

            RayCollision collision =
                GetRayCollisionBox(local_ray, obs.getLocalBox());
            if (collision.hit) {
              float dist_to_tgt_local = Vector3Distance(local_obs, local_tgt);
              if (collision.distance < dist_to_tgt_local) {
                is_blocked = true;
                break;
              }
            }
          }

          if (is_blocked)
            continue;

          bool can_takedown = false;
          bool is_aerial = is_aerial_range && !is_normal_range;
          StealthState s_state = enemy->getStealthComponent().getStealthState();

          bool in_smoke = false;
          for (const auto& sc : smoke_clouds) {
              if (sc.owner != enemy && Vector3DistanceSqr(e_pos, sc.position) <= sc.radius * sc.radius) {
                  in_smoke = true;
                  break;
              }
          }

          // 1. Aerial Takedown
          if (is_aerial && (s_state == StealthState::Unaware ||
                            s_state == StealthState::Suspicious || in_smoke)) {
            can_takedown = true;
          }
          // 2. Stealth Takedown (Must be Unaware or Suspicious, or blinded by smoke, and player
          // closely behind)
          else if (s_state == StealthState::Unaware ||
                   s_state == StealthState::Suspicious || in_smoke) {
            
            Vector3 enemy_fwd = {std::sin(enemy->getRotation().y * DEG2RAD),
                                 0.0f,
                                 std::cos(enemy->getRotation().y * DEG2RAD)};
            Vector3 to_player = Vector3Normalize(Vector3Subtract(p_pos, e_pos));
            float dot = Vector3DotProduct(enemy_fwd, to_player);
            if (dot <
                -0.8f) { // Narrower cone (approx 74 degrees directly behind)
              can_takedown = true;
            }
          }
          // 3. Combat Deathblow (Enemy posture broken)
          else if (enemy->getStats().isPostureBroken()) {
            can_takedown = true;
          }

          if (can_takedown) {
            if (is_aerial) {
              // Trigger the drop phase!
              pending_aerial_target = enemy;
              // Do NOT snap X and Z instantly here; it will lerp smoothly in
              // the update loop Let normal gravity handle the fall instead of
              // boosting it
            } else {
              // Snap rotation and position for grounded takedowns
              if (enemy->getStats().isPostureBroken() &&
                  s_state != StealthState::Unaware &&
                  s_state != StealthState::Suspicious) {
                takedown_type_str = "COMBAT DEATHBLOW";
                // Combat takedown: face the enemy
                Vector3 to_enemy =
                    Vector3Normalize(Vector3Subtract(e_pos, p_pos));
                float target_yaw = std::atan2(to_enemy.x, to_enemy.z) * RAD2DEG;
                player->setRotation({0.0f, target_yaw, 0.0f});

                // Snap position 1.2 units in front of enemy
                player->setPosition({e_pos.x - to_enemy.x * 1.2f, p_pos.y,
                                     e_pos.z - to_enemy.z * 1.2f});
              } else {
                takedown_type_str = "STEALTH TAKEDOWN";
                // Stealth backstab: align exactly with enemy's facing direction
                player->setRotation(enemy->getRotation());

                // Snap position 1.2 units exactly behind the enemy
                float enemy_yaw = enemy->getRotation().y * DEG2RAD;
                Vector3 backward = {-std::sin(enemy_yaw), 0.0f,
                                    -std::cos(enemy_yaw)};
                player->setPosition({e_pos.x + backward.x * 1.2f, p_pos.y,
                                     e_pos.z + backward.z * 1.2f});
                enemy->setKilledByStealth(true);
                enemy->setDecayType(DecayType::ASH_DECAY);
              }

              // Let the hitbox apply the damage and blood in sync with the animation
              enemy->getCombatComponent().setBeingExecuted();
              sound_controller.playSFX(AssetID::SFX_DEATHBLOW);
              player->performTakedown();
              deathblow_victim = enemy;
              takedown_text_timer = 2.0f;
            }
            break; // Only execute one enemy
          }
        }
      }
    }
  }

  // Cleanup fully dissolved bodies at the very end of the frame
  for (auto& enemy : enemies) {
      if (enemy->isFullyDissolved() && !enemy->isModelUnloaded()) {
          if (locked_target == enemy.get()) locked_target = nullptr;
          if (deathblow_victim == enemy.get()) deathblow_victim = nullptr;
          if (pending_aerial_target == enemy.get()) pending_aerial_target = nullptr;
          enemy->setModelUnloaded(true);
      }
  }

  return StateAction::KeepCurrent;
}

void GameplayState::draw() {
  renderList.clear();
  renderList.push_back(player->getRenderData());
  for (const auto &enemy : enemies) {
    if (enemy->isModelUnloaded()) continue;
    renderList.push_back(enemy->getRenderData());
  }

  // PASS 1 — shadow depth. Renders into its own framebuffers, so it has to
  // happen before the scene is cleared, and it needs the same render list the
  // scene pass gets or the shadows would be a frame stale. The player position
  // is what the near cascade recentres on.
  renderer->renderShadowPass(level.obstacles, renderList, player->getPosition());

  // PASS 2 — the scene.
  ClearBackground(RAYWHITE);
  BeginMode3D(camera_controller->getCamera());

  // Level mesh, obstacles and entities, drawn into the 3D scope opened above.
  renderer->renderGameplay(*camera_controller, level.obstacles, renderList);

  // Draw Sword Slash Trails
  player->drawTrail();
  for (const auto &enemy : enemies) {
    if (enemy->isModelUnloaded()) continue;
    enemy->drawTrail();
  }

  particle_manager.draw();

  // Draw Money Drops
  for (const auto& md : money_drops) {
      float y_offset = sinf(md.bob_timer * 3.0f) * 0.2f + 0.5f;
      Vector3 draw_pos = {md.position.x, md.position.y + y_offset, md.position.z};
      DrawSphere(draw_pos, 0.15f, GOLD);
  }

  std::vector<Character *> active_characters;
  active_characters.reserve(1 + enemies.size());
  active_characters.push_back(player.get());
  for (const auto &enemy : enemies) {
    if (enemy->isModelUnloaded()) continue;
    active_characters.push_back(enemy.get());
  }

  // Collision debug wireframes — must stay inside the 3D scope.
  physics_manager.drawDebug(active_characters, level.obstacles);
  combat_manager.drawDebug(active_characters);
  stealth_manager.drawDebug(active_characters);

  if (locked_target) {
    Vector3 chest_pos = locked_target->getPosition();
    chest_pos.y += 1.3f; // Approximate chest height
    
    // Pull the sphere towards the camera so it doesn't get submerged in the model
    Vector3 cam_pos = camera_controller->getCamera().position;
    Vector3 to_cam = Vector3Normalize(Vector3Subtract(cam_pos, chest_pos));
    chest_pos = Vector3Add(chest_pos, Vector3Scale(to_cam, locked_target->getColliderRadius() + 0.1f));
    
    DrawSphere(chest_pos, 0.04f, WHITE);
  }

  EndMode3D();

  // 2D overlay pass (after the 3D scope is closed).
  renderer->drawUI();
  if (show_shadow_map) {
    renderer->drawShadowMapDebug();
  }

  // --- HEALTH BARS ---
  player->drawHPBar2D();
  for (const auto &enemy : enemies) {
    if (enemy->isModelUnloaded()) continue;
    enemy->drawHPBar(camera_controller->getCamera());
  }

  if (takedown_text_timer > 0.0f) {
    const char *text = takedown_type_str.c_str();
    int font_size = 40;
    int text_width = MeasureText(text, font_size);
    DrawText(text, GetScreenWidth() / 2 - text_width / 2,
             GetScreenHeight() / 2 - 100, font_size, RED);
  }

  // --- SMOKE SCREEN EFFECT ---
  if (player->isInSmoke()) {
      // Draw a full-screen semi-transparent gray overlay
      DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), {100, 100, 100, 150});
  }

  // --- MONEY UI ---
  if (IsKeyDown(KEY_I)) {
      std::string money_str = "Money: " + std::to_string(player->getMoney());
      int font_size = 30;
      int text_width = MeasureText(money_str.c_str(), font_size);
      DrawText(money_str.c_str(), GetScreenWidth() - text_width - 30, 30, font_size, GOLD);
  }

  // The F4 readout, latched for a few seconds so it can be read and retyped.
  if (spawn_line_timer > 0.0f && !spawn_line.empty()) {
    const int width = MeasureText(spawn_line.c_str(), 20);
    DrawText(spawn_line.c_str(), (GetScreenWidth() - width) / 2,
             GetScreenHeight() - 70, 20, RAYWHITE);
    DrawText("paste into assets/levels/<phase>/enemies.json, then F5",
             (GetScreenWidth() - MeasureText(
                  "paste into assets/levels/<phase>/enemies.json, then F5", 16)) / 2,
             GetScreenHeight() - 44, 16, GRAY);
  }

  // A broken enemies.json, said out loud.
  //
  // Ungated by any key and drawn every frame it applies, because TraceLog goes
  // to a terminal nobody is looking at while they are inside the game. Without
  // this, "the level loads with no enemies" is indistinguishable from "the
  // level has no enemies", which is the one confusion the loud-failure policy
  // exists to prevent.
  if (!level.enemyOverlayError.empty()) {
    const std::string banner = "enemies.json: " + level.enemyOverlayError;
    DrawRectangle(0, 0, GetScreenWidth(), 34, Fade(BLACK, 0.7f));
    DrawText(banner.c_str(), 12, 8, 20, RED);
  }

  // --- ITEM UI ---
  const auto& inventory = player->getInventory();
  if (!inventory.empty()) {
      int active_idx = player->getActiveItemIndex();
      if (active_idx >= 0 && active_idx < inventory.size()) {
          const auto& active_item = inventory[active_idx];
          std::string item_text = "[Q] < " + active_item->getName() + " (" + std::to_string(active_item->getCount()) + ") > [E]   Use: [X]";
          int item_font_size = 20;
          DrawText(item_text.c_str(), 20, GetScreenHeight() - 70, item_font_size, active_item->isEmpty() ? GRAY : BLACK);

          // If using, draw a progress bar
          float use_timer = player->getItemUseTimer();
          if (use_timer > 0.0f) {
              float duration = active_item->getUseDuration();
              float progress = 1.0f - (use_timer / duration);
              DrawRectangle(20, GetScreenHeight() - 85, 200, 10, DARKGRAY);
              DrawRectangle(20, GetScreenHeight() - 85, (int)(200 * progress), 10, SKYBLUE);
          }
      }
  }
}

void GameplayState::exit() {
  // Clean up local menu resources here
}