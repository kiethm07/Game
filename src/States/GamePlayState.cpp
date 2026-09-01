#include "States/GamePlayState.h"
#include <Core/AssetPaths.h>
#include "raylib.h"
#include "raymath.h"
#include <Core/Game.h>
#include <Level/SpawnGround.h>
#include <Physics/CollisionMeshLoader.h>
#include <Stealth/Sensor.h>
#include <Rendering/BoneSocketHelper.h>
#include <Rendering/Lighting.h>
#include <Rendering/PostureMeter.h>
#include <cassert>
#include <cmath>
#include <fstream>
#include <rlgl.h>

namespace {
/// How close the player must be to a campfire to light it, in metres.
///
/// Measured in the ground plane only. Height is deliberately ignored: the
/// campfire sits on terrain that may slope, and a player standing beside it on
/// a rise is plainly "at" it even though their feet are half a metre higher.
constexpr float CHECKPOINT_REACH = 2.0f;

/// Seconds between the fire catching and the phase ending.
constexpr float CHECKPOINT_HOLD = 2.0f;

/// The deathblow marker: a glowing red orb on the chest of an enemy the player
/// could execute right now -- a broken guard, or an unaware one with the player
/// at their back.
///
/// Drawn from a generated radial gradient rather than an image file. A dot with
/// a soft falloff is two calls to GenImageGradientRadial and no asset to ship,
/// load, downscale or keep in step with the art; assets/PostureBreakCue.png is
/// left on disk but nothing reads it any more.
constexpr int POSTURE_CUE_TEX_PX = 128;

/// Where the orb sits along the chest bone, in that bone's own frame.
///
/// The position comes from `mixamorig:Spine2` -- the upper chest -- so it rides
/// the animation instead of hanging at a fixed height over the character's
/// feet. Measured on the rig: the bone itself sits 1.24 m above the feet, and
/// its local +Y runs up the spine, so this nudges the orb ~0.11 m further up to
/// about 1.35 m -- the breastbone on a 2 m body.
///
/// Vertical only. The bone's local X and Z are lateral, and pushing along them
/// slides the orb around the ribcage rather than out of it; getting clear of
/// the mesh is POSTURE_CUE_LIFT's job, in world space, where the direction that
/// matters is toward the camera.
constexpr Vector3 POSTURE_CUE_BONE_OFFSET = {0.0f, 0.12f, 0.0f};

/// Fallback height above the feet, used only when the rig has no chest bone to
/// sample. Static, so it will not follow an animation -- but a marker in
/// roughly the right place beats no marker at all.
constexpr float POSTURE_CUE_FALLBACK_HEIGHT = 1.45f;

/// Diameter of the orb's solid body, in metres, and the multipliers for the
/// glow around it and the hot spot inside it. Small and bright: this is a dot,
/// not a decal.
constexpr float POSTURE_CUE_BODY_SIZE = 0.20f;
constexpr float POSTURE_CUE_HALO_SCALE = 3.0f;
constexpr float POSTURE_CUE_HOT_SCALE = 0.40f;

/// The defeat screen. Fractions of the screen, never pixels, for the reason
/// MainMenuState's layout block gives: a hardcoded rectangle is only centred at
/// the one resolution it was typed at.
constexpr float DEFEAT_BUTTON_W = 0.2343f;   // 320 px at 1366, the menu's width
constexpr float DEFEAT_BUTTON_H = 0.0729f;   // 56 px at 768
constexpr float DEFEAT_BUTTON_GAP = 0.0293f; // 40 px at 1366, between the pair
constexpr float DEFEAT_BUTTON_TOP = 0.60f;

/// Bone white, the same ink the menu writes its chapters in -- the two screens
/// are the same UI seen at opposite ends of a run.
constexpr Color DEFEAT_INK = {235, 232, 225, 255};

/// The banner. Deep red on the wash rather than pure #FF0000, which on a grey
/// field vibrates rather than reads.
constexpr Color DEFEAT_RED = {168, 18, 20, 255};

/// Clearance added to the body radius when pulling the orb toward the camera.
///
/// The chest bone sits on the character's CENTRE line, not on their chest
/// surface, so the push has to clear a whole body radius before it is out in
/// the open -- a smaller nudge leaves the billboard inside the mesh, losing the
/// depth test, and the marker simply never appears. The lock-on dot below pushes
/// by the same radius for the same reason.
constexpr float POSTURE_CUE_LIFT = 0.15f;
} // namespace

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
             campaign.currentName(), campaign.currentLevelPath().c_str());
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

  // Load SFX
  asset_manager.loadSound(AssetID::SFX_COIN, assets::path("audio/coin.wav"));
  asset_manager.loadSound(AssetID::SFX_HIT, assets::path("audio/hit.MP3"));
  asset_manager.loadSound(AssetID::SFX_DASH, assets::path("audio/dash.mp3"));
  asset_manager.loadSound(AssetID::SFX_DEFLECT_1, assets::path("audio/deflect_1.MP3"));
  asset_manager.loadSound(AssetID::SFX_DEFLECT_2, assets::path("audio/Deflect_2.MP3"));
  asset_manager.loadSound(AssetID::SFX_DEFLECT_NPC, assets::path("audio/deflect_NPC.MP3"));
  asset_manager.loadSound(AssetID::SFX_BLOCK, assets::path("audio/block.MP3"));
  asset_manager.loadSound(AssetID::SFX_DEATHBLOW, assets::path("audio/deflect_end.mp3"));
  asset_manager.loadSound(AssetID::SFX_WALK, assets::path("audio/walkingongrass.mp3"));
  asset_manager.loadSound(AssetID::SFX_RUN, assets::path("audio/runningongrass.wav"));
  asset_manager.loadSound(AssetID::SFX_LAND, assets::path("audio/landingongrass.mp3"));
  asset_manager.loadSound(AssetID::SFX_SLASH, assets::path("audio/swordslash.mp3"));
  asset_manager.loadSound(AssetID::SFX_HEAL, assets::path("audio/healinggourd.mp3"));
  asset_manager.loadSound(AssetID::SFX_SMOKE, assets::path("audio/smokebomb.mp3"));
  asset_manager.loadSound(AssetID::SFX_BREAK, assets::path("audio/posturebreak.mp3"));

  // Load BGMs
  asset_manager.loadMusic(AssetID::BGM_COMBAT, assets::path("audio/incombatbgm.mp3"));
  asset_manager.loadMusic(AssetID::BGM_EXPLORE, assets::path("audio/outcombatbgm.mp3"));

  Vector3 spawn_pos = level.playerSpawn.position;
  float spawn_ground = spawn_pos.y;
  if (SpawnGround::highestUnder(collision_mesh, level.obstacles, level.bounds,
                                spawn_pos.x, spawn_pos.z, spawn_ground)) {
    spawn_pos.y = spawn_ground;
  }
  player->setPosition(spawn_pos);
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

  // This phase's exit campfire, if it has one. Placed before the enemies purely
  // so the ground query it shares with them reads in one block.
  //
  // Only X and Z are authored; the height comes off the collision mesh exactly
  // as a `y`-less enemy spawn's does, so the two numbers in CampaignManifest.h
  // stay correct if the terrain is ever re-exported at a different height.
  if (campaign.hasExit()) {
    Checkpoint exit;
    exit.position = {campaign.exitX(), 0.0f, campaign.exitZ()};
    float ground = 0.0f;
    if (SpawnGround::highestUnder(collision_mesh, level.obstacles, level.bounds,
                                  exit.position.x, exit.position.z, ground)) {
      exit.position.y = ground;
    } else {
      TraceLog(LOG_WARNING,
               "GameplayState: phase %d's exit campfire at (%.2f, %.2f) has no "
               "ground under it; it will sit at y=0. Check the coordinates in "
               "CampaignManifest.h against the level.",
               (int)campaign.index() + 1, exit.position.x, exit.position.z);
    }
    checkpoints.push_back(exit);
    TraceLog(LOG_INFO,
             "GameplayState: exit campfire at (%.2f, %.2f, %.2f)",
             exit.position.x, exit.position.y, exit.position.z);
  }

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

    // Snap patrol waypoint heights if not explicitly authored
    for (Vector3 &wp : placed.patrolPoints) {
      if (std::abs(wp.y) < 0.001f) {
        float wp_ground = 0.0f;
        if (SpawnGround::highestUnder(collision_mesh, level.obstacles,
                                      level.bounds, wp.x, wp.z, wp_ground)) {
          wp.y = wp_ground;
        }
      }
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
  renderer->setCheckpoints(checkpoints);
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

GameplayState::~GameplayState() {
  // exit() has already run on every path Game takes -- popState calls it -- so
  // this is belt and braces for a state destroyed without one.
  unloadPostureCue();
}

void GameplayState::enter() {
  // Mouse-look needs the pointer hidden and locked to the window. Declared here
  // rather than once at startup so that every route into gameplay is covered --
  // from the menu, from a phase change, and from an F5 reload, which never
  // passes through either of the others.
  DisableCursor();

  // White in the middle falling off to transparent. White rather than red
  // because the draw tints it -- one texture then serves both the hot core and
  // the halo, at different sizes and colours.
  Image glow = GenImageGradientRadial(POSTURE_CUE_TEX_PX, POSTURE_CUE_TEX_PX,
                                      0.0f, WHITE, BLANK);
  posture_cue = LoadTextureFromImage(glow);
  UnloadImage(glow);

  if (posture_cue.id != 0) {
    // Drawn at a size that changes with distance, so it is always resampled.
    SetTextureFilter(posture_cue, TEXTURE_FILTER_BILINEAR);
  } else {
    TraceLog(LOG_WARNING,
             "GameplayState: could not generate the deathblow marker texture; "
             "posture-broken enemies will show no marker. The deathblow itself "
             "still works.");
  }

  sound_controller.playMusic(AssetID::BGM_EXPLORE);
}

void GameplayState::exit() {
  unloadPostureCue();
}

void GameplayState::unloadPostureCue() {
  UnloadTexture(posture_cue); // a no-op when id == 0
  posture_cue = Texture2D{};  // so a second call cannot double-free
}

const Enemy *GameplayState::blockingBoss() const {
  for (const auto &enemy_ptr : enemies) {
    const Enemy *enemy = enemy_ptr.get();
    // Dead is enough. Not `isFullyDissolved`, which is a rendering state that
    // takes seconds to arrive -- being made to stand over the body waiting for
    // it to fade would read as the gate being broken.
    if (isBossType(enemy->getType()) && !enemy->getStats().isDead()) {
      return enemy;
    }
  }
  return nullptr;
}

bool GameplayState::hasEngagedEnemy() const {
  if (locked_target != nullptr && !locked_target->getStats().isDead()) {
    return true;
  }
  for (const auto &enemy_ptr : enemies) {
    const Enemy *enemy = enemy_ptr.get();
    if (!enemy->getStats().isDead() && !enemy->isModelUnloaded()) {
      if (enemy->getStealthComponent().isPlayerDetected()) {
        return true;
      }
    }
  }
  return false;
}

GameplayState::TakedownKind
GameplayState::availableTakedown(const Enemy &enemy) const {
  if (enemy.getStats().isDead()) return TakedownKind::None;

  const Vector3 p_pos = player->getPosition();
  const Vector3 e_pos = enemy.getPosition();

  const Vector2 p2 = {p_pos.x, p_pos.z};
  const Vector2 e2 = {e_pos.x, e_pos.z};
  const float horiz_dist = Vector2Distance(p2, e2);
  const float vert_dist = p_pos.y - e_pos.y;

  // Tighter vertical check for grounded takedowns so you don't do grounded
  // takedowns from a ledge.
  const bool is_normal_range =
      (horiz_dist < 2.0f && std::abs(vert_dist) < 0.5f);
  const bool is_aerial_range = (horiz_dist < 2.5f && vert_dist >= 1.5f &&
                                vert_dist < 10.0f && !player->isGrounded());

  if (!is_normal_range && !is_aerial_range) return TakedownKind::None;

  // Raycast check to prevent takedowns through walls. After the range test on
  // purpose: this walks every obstacle in the level, and the range test throws
  // out all but the one or two enemies that could possibly qualify.
  const Vector3 p_head = {p_pos.x, p_pos.y + player->getColliderHeight() * 0.8f,
                          p_pos.z};
  const Vector3 e_head = {e_pos.x, e_pos.y + enemy.getColliderHeight() * 0.8f,
                          e_pos.z};

  for (const auto &obs : level.obstacles) {
    Vector3 local_obs = Vector3Transform(p_head, obs.getWorldToLocal());
    Vector3 local_tgt = Vector3Transform(e_head, obs.getWorldToLocal());

    Ray local_ray;
    local_ray.position = local_obs;
    local_ray.direction =
        Vector3Normalize(Vector3Subtract(local_tgt, local_obs));

    RayCollision collision = GetRayCollisionBox(local_ray, obs.getLocalBox());
    if (collision.hit) {
      float dist_to_tgt_local = Vector3Distance(local_obs, local_tgt);
      if (collision.distance < dist_to_tgt_local) {
        return TakedownKind::None;
      }
    }
  }

  const bool is_aerial = is_aerial_range && !is_normal_range;
  const StealthState s_state = enemy.getStealthComponent().getStealthState();

  bool in_smoke = false;
  for (const auto &sc : smoke_clouds) {
    if (sc.owner != &enemy &&
        Vector3DistanceSqr(e_pos, sc.position) <= sc.radius * sc.radius) {
      in_smoke = true;
      break;
    }
  }

  const bool off_guard = (s_state == StealthState::Unaware ||
                          s_state == StealthState::Suspicious || in_smoke);

  // A boss is never a stealth kill, from behind or from the air. Both routes
  // end the fight on contact, and a fight the whole phase is gated on cannot be
  // skippable by walking up behind it -- which is exactly what happened, since
  // a boss standing at its post has never seen the player and so is Unaware,
  // the same state that makes a patrolling mook backstabbable.
  //
  // Answered here rather than at the Takedown key because the chest marker asks
  // the same question: gating only the key would leave a deathblow cue lit over
  // a boss that F then refuses.
  const bool is_boss = isBossType(enemy.getType());

  // The order is a chain, not three independent tests, and it stays one: an
  // enemy who is off guard is a stealth kill or nothing, even if their posture
  // also happens to be broken. Flattening this into three ORs would quietly
  // let you combat-deathblow someone from the front because they were unaware.
  //
  // A boss therefore returns None while off guard rather than falling through
  // to the posture test below. That is the same rule, not an exception to it:
  // the combat deathblow is meant to be the reward for breaking a guard that
  // was actually up, and letting an unaware boss be executed from the front
  // because its posture happened to still be high is the very hole the chain
  // exists to close.
  if (is_aerial && off_guard) {
    return is_boss ? TakedownKind::None : TakedownKind::Aerial;
  }
  if (off_guard) {
    if (is_boss) return TakedownKind::None;

    // Must be closely behind them -- roughly a 74 degree cone off their back.
    const Vector3 enemy_fwd = {std::sin(enemy.getRotation().y * DEG2RAD), 0.0f,
                               std::cos(enemy.getRotation().y * DEG2RAD)};
    const Vector3 to_player = Vector3Normalize(Vector3Subtract(p_pos, e_pos));
    if (Vector3DotProduct(enemy_fwd, to_player) < -0.8f) {
      return TakedownKind::Stealth;
    }
    return TakedownKind::None;
  }
  if (enemy.getStats().isPostureBroken()) {
    return TakedownKind::Combat;
  }
  return TakedownKind::None;
}

void GameplayState::drawBossPostureBars() {
  const float screen_w = static_cast<float>(GetScreenWidth());
  float y = 54.0f;

  for (const auto &enemy_ptr : enemies) {
    const Enemy *enemy = enemy_ptr.get();
    if (!isBossType(enemy->getType())) continue;
    if (enemy->getStats().isDead() || enemy->isModelUnloaded()) continue;

    // Aggro, and only aggro. A boss standing unaware across the arena is not a
    // fight yet, and a bar arriving before it has noticed the player would
    // announce which of the figures over there is the boss.
    const bool aggro = enemy->getStealthComponent().isPlayerDetected() ||
                       locked_target == static_cast<const Character *>(enemy);
    if (!aggro) continue;

    PostureMeter::Style style;
    style.half_width = screen_w * 0.20f;
    style.height = 13.0f;
    style.cap = style.half_width * 0.11f;
    style.fill = PostureMeter::kEnemyFill;

    PostureMeter::draw(screen_w * 0.5f, y,
                       enemy->getStats().getPosturePercentage(), style);

    // No phase authors two bosses awake at once today, but stacking costs one
    // line and beats drawing the second bar exactly on top of the first.
    y += style.height + 14.0f;
  }
}

void GameplayState::drawPostureCues() {
  if (posture_cue.id == 0) return;

  // The same two global gates the Takedown key is behind. Without them the orb
  // stays lit through the execution the player already started.
  if (pending_aerial_target != nullptr || player->isExecuting()) return;

  const Camera3D camera = camera_controller->getCamera();

  for (const auto &enemy_ptr : enemies) {
    const Enemy *enemy = enemy_ptr.get();
    if (enemy->isModelUnloaded()) continue;

    // Marks every deathblow the player could actually take right now, by asking
    // the same question the Takedown key asks -- so it lights up for a stealth
    // kill from behind an unaware enemy just as it does for a broken guard, and
    // it is never showing when F would refuse.
    if (availableTakedown(*enemy) == TakedownKind::None) continue;

    // Once the takedown is under way the offer has been taken. Leaving the cue
    // up through the animation would read as a second deathblow being available
    // on a corpse.
    if (enemy->isBeingExecuted()) continue;

    // Off the animated skeleton, so the orb rides the chest through every
    // stagger, breath and turn instead of hanging in the air where the
    // character's feet happen to be. Falls back to a fixed height only if the
    // rig has no spine bone to sample.
    Vector3 chest;
    if (!BoneSocketHelper::sampleChestPoint(asset_manager, enemy->getRenderData(),
                                            chest, POSTURE_CUE_BONE_OFFSET)) {
      chest = enemy->getPosition();
      chest.y += POSTURE_CUE_FALLBACK_HEIGHT;
    }

    // Out of the body toward the camera, or the billboard is a flat quad buried
    // inside a solid model. Same treatment as the lock-on dot below.
    const Vector3 to_cam =
        Vector3Normalize(Vector3Subtract(camera.position, chest));
    chest = Vector3Add(
        chest,
        Vector3Scale(to_cam, enemy->getColliderRadius() + POSTURE_CUE_LIFT));

    // Depth WRITES off, depth testing on. The three layers overlap, and if the
    // first wrote depth the rest would fail the test against it and vanish;
    // testing stays on so the world still occludes the orb.
    rlDisableDepthMask();

    // Outer glow: additive, because light spilling onto what is behind it is
    // exactly what additive does.
    BeginBlendMode(BLEND_ADDITIVE);
    DrawBillboard(camera, posture_cue, chest,
                  POSTURE_CUE_BODY_SIZE * POSTURE_CUE_HALO_SCALE,
                  Color{190, 20, 10, 255});
    EndBlendMode();

    // The orb itself: ALPHA, not additive, and that is the whole difference
    // between a red dot and a yellow one. Additive can only add, so over lit
    // grass the green and blue channels saturate alongside the red and the
    // marker washes out to white-yellow -- which is what the first version of
    // this did. Alpha replaces the background instead, so the orb stays the
    // colour it is told to be whatever it is standing in front of.
    DrawBillboard(camera, posture_cue, chest, POSTURE_CUE_BODY_SIZE,
                  Color{255, 40, 25, 255});

    // Hot centre, back to additive: this one is meant to blow out.
    BeginBlendMode(BLEND_ADDITIVE);
    DrawBillboard(camera, posture_cue, chest,
                  POSTURE_CUE_BODY_SIZE * POSTURE_CUE_HOT_SCALE,
                  Color{255, 200, 180, 255});
    EndBlendMode();

    rlEnableDepthMask();
  }
}

StateAction GameplayState::update(float dt) {

  // 0. Death, and the two things it costs the frame.
  //
  // Once the banner is up the world is not ticked at all -- draw() still runs,
  // so the scene stays on screen under the wash, but nothing in it moves. That
  // is what stops the enemies who won the fight from carrying on hacking at a
  // corpse behind a menu the player is reading.
  if (defeat_shown)
    return updateDefeatScreen();

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
                          &smoke_clouds, locked_target, &active_characters,
                          &sound_controller};

  // Adaptive BGM evaluation based on engaged enemies
  if (hasEngagedEnemy()) {
    sound_controller.playMusic(AssetID::BGM_COMBAT);
  } else {
    sound_controller.playMusic(AssetID::BGM_EXPLORE);
  }

  // 1.5. Evaluate Stealth before AI update so AI can react in the same frame
  stealth_manager.update(active_characters, player.get(), level.obstacles,
                         collision_mesh.isEmpty() ? nullptr : &collision_mesh,
                         smoke_clouds, dt);

  player->update(ctx);

  // 1.6 The death clock.
  //
  // After player->update() and not before it, which is what makes
  // deathAnimDuration() answerable: the animator resolves its clip names on the
  // player's first update, and a clock started ahead of that would measure the
  // fall as zero and put the banner up while the body was still going down. The
  // ordering is only load-bearing on the first frame of a phase, which is
  // exactly the frame no honest death can happen on -- so getting it wrong is
  // invisible until something else makes that frame reachable.
  //
  // The world keeps running for the whole wait: the camera follows, bodies go
  // on dissolving, the killing blow's blood goes on falling. Player::update
  // reads the same isDead() and has already handed the character to the death
  // clip, so nothing here has to stop the player -- only to time how long the
  // fall is watched for.
  if (player->getStats().isDead()) {
    if (death_timer < 0.0f) {
      death_timer = 0.0f;
      defeat_at = player->deathAnimDuration(asset_manager);

      // Dropped here rather than left to the checks further down. A lock-on
      // that outlived its holder would hold the camera on an enemy through the
      // whole fall, framing the fight the player just lost instead of the loss.
      locked_target = nullptr;
      deathblow_victim = nullptr;
      pending_aerial_target = nullptr;

      TraceLog(LOG_INFO,
               "GameplayState: player died in phase %d/%d; defeat screen in "
               "%.2fs, the length of the fall",
               (int)campaign.index() + 1, (int)campaign.count(), defeat_at);
    }

    death_timer += dt;
    if (!defeat_shown && death_timer >= defeat_at) {
      defeat_shown = true;
      // The screen is driven by the mouse, so the pointer has to come back --
      // enter() locked it away for mouse-look. Every route out of this state
      // sets its own mode in enter(), so nothing has to put it back.
      EnableCursor();
      buildDefeatButtons();
      // Deliberately not an early return: this frame finishes as an ordinary
      // one, and the branch at the top of update() takes the next one. A return
      // here would drop the physics and combat passes for a single frame purely
      // to save them, which is the kind of asymmetry that later reads as a bug.
    }
  }

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

  if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE)) {
    campaign.resetCompleted();
    return StateAction::ChangeToMenu;
  }

  // Exit handling: campfire or victory on the final phase
  checkpoint_in_reach = false;
  checkpoint_blocked_by = nullptr;
  if (!campaign.hasExit()) {
    if (blockingBoss() == nullptr && death_timer < 0.0f) {
      if (victory_timer < 0.0f) {
        victory_timer = VICTORY_HOLD;
        TraceLog(LOG_INFO,
                 "GameplayState: Final boss defeated — transitioning to victory in %.1fs",
                 VICTORY_HOLD);
      } else {
        victory_timer -= dt;
        if (victory_timer <= 0.0f) {
          victory_timer = -1.0f;
          campaign.setCarry(snapshotCarry());
          return StateAction::RequestNextPhase;
        }
      }
    }
  }

  if (checkpoint_timer >= 0.0f) {
    // A countdown is already running. Nothing can start a second one, and the
    // fire is left burning while it runs -- that pause is the whole point.
    checkpoint_timer -= dt;
    if (checkpoint_timer <= 0.0f) {
      checkpoint_timer = -1.0f;
      campaign.setCarry(snapshotCarry());
      return StateAction::RequestNextPhase;
    }
  } else if (death_timer < 0.0f) {
    // A corpse cannot rest. Guarded here rather than at the keypress so the
    // prompt goes away too: a player bleeding out on top of the exit campfire
    // would otherwise be told to press G, press it, and end the phase they
    // just lost. A countdown already running is deliberately left to finish --
    // that fire was lit by someone who was still alive.
    const Vector3 here = player->getPosition();
    for (size_t i = 0; i < checkpoints.size(); ++i) {
      if (checkpoints[i].lit) continue;
      // Plan distance only. The campfire sits on terrain that may slope, and a
      // player standing beside it on a rise is plainly at it even though their
      // feet are higher.
      const float dx = here.x - checkpoints[i].position.x;
      const float dz = here.z - checkpoints[i].position.z;
      if (dx * dx + dz * dz > CHECKPOINT_REACH * CHECKPOINT_REACH) continue;

      checkpoint_in_reach = true;

      // A phase with a boss in it does not end until the boss does. Checked
      // here rather than at the keypress so the prompt can say so on approach
      // -- being told why after pressing G is how a player concludes the key
      // is broken.
      checkpoint_blocked_by = blockingBoss();
      if (checkpoint_blocked_by != nullptr) {
        break;
      }

      if (input_manager.isActionPressed(GameAction::Interact)) {
        checkpoints[i].lit = true;
        renderer->setCheckpoints(checkpoints);
        checkpoint_timer = CHECKPOINT_HOLD;
        pending_checkpoint = (int)i;
        TraceLog(LOG_INFO,
                 "GameplayState: campfire lit — leaving phase %d/%d in %.1fs",
                 (int)campaign.index() + 1, (int)campaign.count(),
                 CHECKPOINT_HOLD);
      }
      break;  // Only ever the first one in reach; they are metres apart.
    }
  }

  // Debug: I key toggles Ghost mode (allows walking through NPCs)
  if (IsKeyPressed(KEY_I)) {
    if (ghost_mode) {
      ghost_mode = false;
    } else {
      ghost_mode = true;
    }
    player->setGhost(ghost_mode);
    if (ghost_mode) {
      TraceLog(LOG_INFO, "GameplayState: Ghost mode ENABLED (pass-through NPCs)");
    } else {
      TraceLog(LOG_INFO, "GameplayState: Ghost mode DISABLED");
    }
  }

  // Debug: P key outputs player's location and yaw to text file
  if (IsKeyPressed(KEY_P)) {
    const Vector3 p = player->getPosition();
    float yaw = player->getRotation().y;
    while (yaw <= -180.0f) {
      yaw += 360.0f;
    }
    while (yaw > 180.0f) {
      yaw -= 360.0f;
    }

    std::ofstream out_file("recorded_npc_positions.txt", std::ios::app);
    if (out_file.is_open()) {
      out_file << "{ \"type\": \"Swordman\", \"x\": " << p.x << ", \"y\": " << p.y
               << ", \"z\": " << p.z << ", \"yaw\": " << yaw << ", \"wanderRadius\": 0.0 },\n";
      out_file.close();
    }
    last_saved_pos_str = TextFormat("Saved (%.2f, %.2f, %.2f) Yaw: %.1f", p.x, p.y, p.z, yaw);
    saved_pos_toast_timer = 2.5f;
    TraceLog(LOG_INFO, "GameplayState: %s", last_saved_pos_str.c_str());
  }

  // Debug: K key cycles / selects an NPC
  if (IsKeyPressed(KEY_K)) {
    selectNextDebugNPC();
  }

  // Debug: O key outputs/records the player's position as a patrol waypoint for the selected NPC
  if (IsKeyPressed(KEY_O)) {
    recordPatrolWaypoint();
  }

  // Debug: N key marks the selected NPC for deletion (records note to file)
  if (IsKeyPressed(KEY_N)) {
    markNPCForDeletion();
  }

  if (saved_pos_toast_timer > 0.0f) {
    saved_pos_toast_timer -= dt;
  }

  // F4: log the player's position to console/terminal.
  if (IsKeyPressed(KEY_F4)) {
    const Vector3 p = player->getPosition();
    float yaw = player->getRotation().y;
    while (yaw <= -180.0f) yaw += 360.0f;
    while (yaw > 180.0f) yaw -= 360.0f;

    std::string spawn_str = TextFormat(
        "{ \"type\": \"%s\", \"x\": %.2f, \"z\": %.2f, \"yaw\": %.1f },",
        enemyTypeName(debug_spawn_type), p.x, p.z, yaw);
    TraceLog(LOG_INFO, "%s", spawn_str.c_str());
    TraceLog(LOG_INFO,
             "  (you are at y=%.2f -- add \"y\": %.2f to pin the height "
             "instead of snapping to the ground)",
             p.y, p.y);
  }

  if (IsKeyPressed(KEY_F4) && IsKeyDown(KEY_LEFT_SHIFT)) {
    debug_spawn_type = static_cast<EnemyType>(
        (static_cast<int>(debug_spawn_type) + 1) % kEnemyTypeCount);
  }

  // F5: rebuild this phase, re-reading level.json and enemies.json.
  if (IsKeyPressed(KEY_F5)) {
    return StateAction::RequestReloadPhase;
  }

  // F6 drops a campfire where the player stands; F7 lights or snuffs nearest.
  if (IsKeyPressed(KEY_F6)) {
    Checkpoint point;
    point.position = player->getPosition();
    point.yaw = player->getRotation().y;
    float ground = 0.0f;
    if (SpawnGround::highestUnder(collision_mesh, level.obstacles, level.bounds,
                                  point.position.x, point.position.z, ground)) {
      point.position.y = ground;
    }
    checkpoints.push_back(point);
    renderer->setCheckpoints(checkpoints);
    TraceLog(LOG_INFO,
             "GameplayState: campfire %d at (%.2f, %.2f, %.2f) — F7 to light it",
             (int)checkpoints.size(), point.position.x, point.position.y,
             point.position.z);
  }

  if (IsKeyPressed(KEY_F7) && !checkpoints.empty()) {
    size_t nearest = 0;
    float best = Vector3DistanceSqr(player->getPosition(),
                                    checkpoints[0].position);
    for (size_t i = 1; i < checkpoints.size(); ++i) {
      const float d = Vector3DistanceSqr(player->getPosition(),
                                         checkpoints[i].position);
      if (d < best) { best = d; nearest = i; }
    }
    checkpoints[nearest].lit = !checkpoints[nearest].lit;
    renderer->setCheckpoints(checkpoints);
    TraceLog(LOG_INFO, "GameplayState: campfire %d is now %s",
             (int)nearest + 1, checkpoints[nearest].lit ? "lit" : "out");
  }

  // F8: die on the spot for testing defeat state.
  if (IsKeyPressed(KEY_F8)) {
    player->takeDamage(99999.0f, 0.0f, nullptr);
  }

  if (smoke_cooldown_timer > 0.0f) {
      smoke_cooldown_timer -= dt;
  }

  // Process smoke clouds spawned by items (e.g. Smoke Bomb)
  std::vector<SmokeCloud> new_clouds = player->takePendingSmokeClouds();
  for (const auto& sc : new_clouds) {
      smoke_clouds.push_back(sc);
      particle_manager.emitVisualSmoke(sc.position, sc.radius, sc.life);
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
        StealthManager::emitNoise(pending_aerial_target->getPosition(), 15.0f, active_characters, player.get());

        pending_aerial_target = nullptr;
      }
    }
  }

  // Takedown logic
  if (!pending_aerial_target && !player->isExecuting() &&
      input_manager.isActionPressed(GameAction::Takedown)) {
    for (auto &enemy_ptr : enemies) {
      Enemy *enemy = enemy_ptr.get();

      // Range, line of sight and the three routes all live in
      // availableTakedown, which the chest marker reads too. Keeping that
      // decision in one place is what stops the marker and this key disagreeing
      // about who can be executed.
      const TakedownKind kind = availableTakedown(*enemy);
      if (kind != TakedownKind::None) {
        const bool is_aerial = (kind == TakedownKind::Aerial);

        Vector3 p_pos = player->getPosition();
        Vector3 e_pos = enemy->getPosition();
        StealthState s_state = enemy->getStealthComponent().getStealthState();

        {
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
              StealthManager::emitNoise(enemy->getPosition(), 15.0f, active_characters, player.get());
            }
            break; // Only execute one enemy
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

void GameplayState::buildDefeatButtons() {
  const float screen_w = static_cast<float>(GetScreenWidth());
  const float screen_h = static_cast<float>(GetScreenHeight());

  const float w = screen_w * DEFEAT_BUTTON_W;
  const float h = screen_h * DEFEAT_BUTTON_H;
  const float gap = screen_w * DEFEAT_BUTTON_GAP;
  const float y = screen_h * DEFEAT_BUTTON_TOP;

  // The pair centred as one block, so neither is centred on the screen and the
  // banner above them is.
  const float left = (screen_w - (w * 2.0f + gap)) * 0.5f;

  defeat_buttons.clear();
  defeat_buttons.push_back(
      DefeatButton{Rectangle{left, y, w, h}, "RETURN TO MENU",
                   StateAction::ChangeToMenu});
  // Not ChangeToGameplay, which would rebuild this phase without Campaign ever
  // hearing about it. RequestReloadPhase is the action that means "this phase
  // again": Game rebuilds the state from the cursor where it already is, and
  // the fresh Player comes up at full health with the carry this phase was
  // entered holding -- which is what retrying a lost fight has to mean.
  defeat_buttons.push_back(
      DefeatButton{Rectangle{left + w + gap, y, w, h}, "RETRY CHAPTER",
                   StateAction::RequestReloadPhase});

  defeat_hovered = -1;
}

StateAction GameplayState::updateDefeatScreen() {
  const Vector2 mouse = GetMousePosition();

  defeat_hovered = -1;
  for (size_t i = 0; i < defeat_buttons.size(); ++i) {
    if (CheckCollisionPointRec(mouse, defeat_buttons[i].bounds)) {
      defeat_hovered = static_cast<int>(i);
      break;
    }
  }

  if (defeat_hovered >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    if (defeat_buttons[defeat_hovered].action == StateAction::ChangeToMenu) {
      campaign.resetCompleted();
    }
    return defeat_buttons[defeat_hovered].action;
  }

  // The same key that leaves a live phase leaves a lost one, and it means the
  // same thing here that it does there. ENTER is deliberately not bound: on a
  // screen whose whole content is two buttons, a key that picks one of them
  // without saying which is a way to lose a run by leaning on the keyboard.
  if (IsKeyPressed(KEY_ESCAPE)) {
    campaign.resetCompleted();
    return StateAction::ChangeToMenu;
  }

  return StateAction::KeepCurrent;
}

void GameplayState::drawDefeatScreen() {
  const float screen_w = static_cast<float>(GetScreenWidth());
  const float screen_h = static_cast<float>(GetScreenHeight());

  // Grey rather than black, and not opaque: the scene stays legible underneath,
  // which is what makes this read as the world draining of colour rather than
  // as a menu that has replaced it.
  DrawRectangle(0, 0, static_cast<int>(screen_w), static_cast<int>(screen_h),
                Color{58, 58, 62, 198});

  const int banner_size = static_cast<int>(screen_h * 0.1302f); // 100 px at 768
  const int banner_w = MeasureText("DEFEATED", banner_size);
  const int banner_x = static_cast<int>((screen_w - banner_w) * 0.5f);
  const int banner_y = static_cast<int>(screen_h * 0.34f);

  // Offset copy first, in near-black. Red on grey is the lowest-contrast pair
  // on this screen, and the drop is what stops the word dissolving into
  // whatever happens to be behind it.
  DrawText("DEFEATED", banner_x + 4, banner_y + 4, banner_size,
           Fade(BLACK, 0.55f));
  DrawText("DEFEATED", banner_x, banner_y, banner_size, DEFEAT_RED);

  // A rule under the banner, the menu's own device at the menu's proportions.
  DrawRectangle(static_cast<int>(screen_w * 0.5f - banner_w * 0.3f),
                banner_y + banner_size + 18,
                static_cast<int>(banner_w * 0.6f), 2, Fade(DEFEAT_RED, 0.7f));

  const int label_size = static_cast<int>(screen_h * 0.0339f); // 26 px at 768
  for (size_t i = 0; i < defeat_buttons.size(); ++i) {
    const DefeatButton &button = defeat_buttons[i];
    const bool lit = (static_cast<int>(i) == defeat_hovered);

    DrawRectangleRec(button.bounds, Fade(BLACK, lit ? 0.65f : 0.45f));
    DrawRectangleLinesEx(button.bounds, lit ? 2.0f : 1.0f,
                         lit ? GOLD : Fade(DEFEAT_INK, 0.35f));
    if (lit) {
      DrawRectangleRec(Rectangle{button.bounds.x, button.bounds.y, 4.0f,
                                 button.bounds.height},
                       GOLD);
    }

    // Centred inside the button, unlike the menu's left-aligned chapter rows:
    // there are two of these side by side, and a ragged inner edge between a
    // pair reads as a misalignment rather than as a style.
    const int label_w = MeasureText(button.label, label_size);
    DrawText(button.label,
             static_cast<int>(button.bounds.x +
                              (button.bounds.width - label_w) * 0.5f),
             static_cast<int>(button.bounds.y +
                              (button.bounds.height - label_size) * 0.5f),
             label_size, lit ? GOLD : DEFEAT_INK);
  }
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
  //
  // Cleared to the fog colour, not to white. Distance fades toward this exact
  // value (Lighting::kSky, which mood_common.glsl fogs to), so the far edge of
  // the terrain dissolves into the sky instead of ending against it.
  ClearBackground(Lighting::kSky);
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

  // Deathblow markers. Before the lock-on dot so that on an enemy who is both
  // locked on and broken, the small solid dot sits on top of the cue rather
  // than being swallowed by it.
  drawPostureCues();

  if (locked_target) {
    Vector3 chest_pos = locked_target->getPosition();
    chest_pos.y += 1.3f; // Approximate chest height
    
    // Pull the sphere towards the camera so it doesn't get submerged in the model
    Vector3 cam_pos = camera_controller->getCamera().position;
    Vector3 to_cam = Vector3Normalize(Vector3Subtract(cam_pos, chest_pos));
    chest_pos = Vector3Add(chest_pos, Vector3Scale(to_cam, locked_target->getColliderRadius() + 0.1f));
    
    DrawSphere(chest_pos, 0.04f, WHITE);
  }

  // Draw player orientation arrow for debug
  drawPlayerOrientationArrow();
  drawEnemyOrientationArrows();
  drawPatrolDebugPath();

  // Draw HitBox & HurtBox combat debug
  std::vector<Character*> debug_characters;
  debug_characters.push_back(player.get());
  for (const auto &enemy : enemies) {
    if (!enemy->isModelUnloaded() && !enemy->getStats().isDead()) {
      debug_characters.push_back(enemy.get());
    }
  }
  combat_manager.drawDebug(debug_characters);

  EndMode3D();

  // 2D overlay pass (after the 3D scope is closed).
  renderer->drawUI();

  // --- HEALTH BARS ---
  const bool player_engaged = hasEngagedEnemy();
  player->drawHPBar2D(player_engaged);
  for (const auto &enemy : enemies) {
    if (enemy->isModelUnloaded()) continue;
    enemy->drawHPBar(camera_controller->getCamera(),
                     locked_target == enemy.get());
  }
  drawBossPostureBars();
  drawEnemyOverheadInfo();

  // --- DEBUG HUD ---
  drawDebugHUD();

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

  // The campfire prompt. Without it the interaction is undiscoverable -- there
  // is nothing about standing near a pile of logs that suggests a key.
  if (checkpoint_in_reach) {
    if (checkpoint_blocked_by != nullptr) {
      // Named off the blocker's own type rather than hardcoded, so the one
      // phase that has a mini boss says "mini boss" and a phase that is ever
      // gated on something else does not lie about which fight is left.
      const char *prompt =
          checkpoint_blocked_by->getType() == EnemyType::MiniBoss
              ? "You must defeat mini boss to proceed"
              : "You must defeat the boss to proceed";
      const int width = MeasureText(prompt, 26);
      DrawText(prompt, (GetScreenWidth() - width) / 2,
               GetScreenHeight() - 120, 26, Color{255, 120, 110, 255});
    } else {
      const char *prompt = "[G]  Light the campfire";
      const int width = MeasureText(prompt, 26);
      DrawText(prompt, (GetScreenWidth() - width) / 2,
               GetScreenHeight() - 120, 26, Color{255, 220, 150, 255});
    }
  }

  // ...and once it is lit, say what is happening, so the two seconds before the
  // screen changes read as deliberate rather than as a hang.
  if (checkpoint_timer >= 0.0f) {
    const char *resting = "Resting...";
    const int width = MeasureText(resting, 30);
    DrawText(resting, (GetScreenWidth() - width) / 2,
             GetScreenHeight() - 120, 30, Color{255, 200, 120, 255});
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

  // --- COIN / ITEM UI ---
  std::string coin_text = "Coins: " + std::to_string(player->getMoney());
  int coin_font_size = 20;
  DrawText(coin_text.c_str(), 20, GetScreenHeight() - 100, coin_font_size, YELLOW);

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

  // Last, over everything: the wash has to fall on the HUD as well as on the
  // world. An empty health bar and a campfire prompt at full brightness under a
  // greyed-out scene would read as the game still being live.
  if (defeat_shown) {
    drawDefeatScreen();
  }
}

void GameplayState::drawPlayerOrientationArrow() const {
  Vector3 pos = player->getPosition();
  pos.y += 1.0f;

  float yaw_rad = player->getRotation().y * DEG2RAD;
  Vector3 forward = {std::sin(yaw_rad), 0.0f, std::cos(yaw_rad)};

  float arrow_len = 1.8f;
  Vector3 end_pos = Vector3Add(pos, Vector3Scale(forward, arrow_len));

  DrawCylinderEx(pos, end_pos, 0.04f, 0.04f, 8, YELLOW);

  Vector3 tip_pos = Vector3Add(end_pos, Vector3Scale(forward, 0.35f));
  DrawCylinderEx(end_pos, tip_pos, 0.12f, 0.0f, 8, RED);
}

void GameplayState::selectNextDebugNPC() {
  const Vector3 player_pos = player->getPosition();
  Enemy *closest_enemy = nullptr;
  float min_dist_sq = 1000000.0f;

  for (const auto &e : enemies) {
    if (!e->isModelUnloaded() && !e->getStats().isDead()) {
      float d_sq = Vector3DistanceSqr(player_pos, e->getPosition());
      if (d_sq < min_dist_sq) {
        min_dist_sq = d_sq;
        closest_enemy = e.get();
      }
    }
  }

  if (closest_enemy == nullptr) {
    selected_debug_npc = nullptr;
    last_saved_pos_str = "No living NPCs found!";
    saved_pos_toast_timer = 2.0f;
    return;
  }

  if (selected_debug_npc != closest_enemy) {
    recorded_waypoints.clear();
  }
  selected_debug_npc = closest_enemy;

  Vector3 p = selected_debug_npc->getPosition();
  float dist = std::sqrt(min_dist_sq);
  last_saved_pos_str = TextFormat("Selected Closest %s (Dist: %.1fm)",
                                  enemyTypeName(selected_debug_npc->getType()),
                                  dist);
  saved_pos_toast_timer = 2.5f;
  TraceLog(LOG_INFO, "GameplayState: %s", last_saved_pos_str.c_str());
}

void GameplayState::recordPatrolWaypoint() {
  if (selected_debug_npc == nullptr || selected_debug_npc->getStats().isDead()) {
    last_saved_pos_str = "No NPC selected! Press 'K' first.";
    saved_pos_toast_timer = 2.0f;
    return;
  }

  const Vector3 p = player->getPosition();
  recorded_waypoints.push_back(p);

  std::ofstream out_file("recorded_patrol_paths.txt", std::ios::app);
  if (out_file.is_open()) {
    Vector3 spawn = selected_debug_npc->getSpawnPosition();
    out_file << "{ \"npcType\": \"" << enemyTypeName(selected_debug_npc->getType())
             << "\", \"npcSpawn\": { \"x\": " << spawn.x << ", \"y\": " << spawn.y << ", \"z\": " << spawn.z << " }"
             << ", \"waypointIndex\": " << recorded_waypoints.size()
             << ", \"waypoint\": { \"x\": " << p.x << ", \"y\": " << p.y << ", \"z\": " << p.z << " }"
             << ", \"patrolMode\": \"2-Way (Ping-Pong)\" },\n";
    out_file.close();
  }

  last_saved_pos_str = TextFormat("Waypoint #%d for %s (%.2f, %.2f, %.2f)",
                                  static_cast<int>(recorded_waypoints.size()),
                                  enemyTypeName(selected_debug_npc->getType()),
                                  p.x, p.y, p.z);
  saved_pos_toast_timer = 2.5f;
  TraceLog(LOG_INFO, "GameplayState: %s", last_saved_pos_str.c_str());
}

void GameplayState::markNPCForDeletion() {
  if (selected_debug_npc == nullptr || selected_debug_npc->getStats().isDead()) {
    last_saved_pos_str = "No NPC selected to mark for deletion! Press 'K'.";
    saved_pos_toast_timer = 2.0f;
    return;
  }

  bool already_marked = false;
  for (const Enemy *marked : marked_deletion_npcs) {
    if (marked == selected_debug_npc) {
      already_marked = true;
      break;
    }
  }

  if (!already_marked) {
    marked_deletion_npcs.push_back(selected_debug_npc);
  }

  Vector3 spawn = selected_debug_npc->getSpawnPosition();
  float yaw = selected_debug_npc->getSpawnYaw();

  std::ofstream out_file("recorded_npc_deletions.txt", std::ios::app);
  if (out_file.is_open()) {
    out_file << "{ \"action\": \"DELETE\", \"type\": \"" << enemyTypeName(selected_debug_npc->getType())
             << "\", \"x\": " << spawn.x << ", \"y\": " << spawn.y << ", \"z\": " << spawn.z
             << ", \"yaw\": " << yaw << " },\n";
    out_file.close();
  }

  last_saved_pos_str = TextFormat("Marked %s for deletion (Logged)",
                                  enemyTypeName(selected_debug_npc->getType()));
  saved_pos_toast_timer = 2.5f;
  TraceLog(LOG_INFO, "GameplayState: %s", last_saved_pos_str.c_str());
}

void GameplayState::drawPatrolDebugPath() const {
  // Highlight selected NPC with a glowing cyan ring under feet
  if (selected_debug_npc != nullptr && !selected_debug_npc->getStats().isDead()) {
    Vector3 foot_pos = selected_debug_npc->getPosition();
    foot_pos.y += 0.2f;
    float r = selected_debug_npc->getColliderRadius() + 0.35f;
    DrawCylinderWires(foot_pos, r, r, 0.4f, 16, SKYBLUE);
    DrawCircle3D(selected_debug_npc->getPosition(), r + 0.05f, {1, 0, 0}, 90.0f, SKYBLUE);
  }

  // Highlight marked for deletion NPCs with a red ring
  for (const Enemy *marked : marked_deletion_npcs) {
    if (marked != nullptr && !marked->getStats().isDead()) {
      Vector3 foot_pos = marked->getPosition();
      foot_pos.y += 0.2f;
      float r = marked->getColliderRadius() + 0.35f;
      DrawCylinderWires(foot_pos, r, r, 0.4f, 16, MAROON);
      DrawCircle3D(marked->getPosition(), r + 0.05f, {1, 0, 0}, 90.0f, RED);
    }
  }

  // Draw recorded waypoints and connecting lines
  if (!recorded_waypoints.empty()) {
    for (size_t i = 0; i < recorded_waypoints.size(); ++i) {
      Vector3 wp = recorded_waypoints[i];
      wp.y += 0.3f;
      DrawSphere(wp, 0.25f, LIME);
      DrawSphereWires(wp, 0.26f, 8, 8, DARKGREEN);

      if (i > 0) {
        Vector3 prev_wp = recorded_waypoints[i - 1];
        prev_wp.y += 0.3f;
        DrawCylinderEx(prev_wp, wp, 0.04f, 0.04f, 6, YELLOW);
      }
    }
  }
}

void GameplayState::drawDebugHUD() const {
  const Vector3 p = player->getPosition();
  float yaw = player->getRotation().y;
  while (yaw <= -180.0f) {
    yaw += 360.0f;
  }
  while (yaw > 180.0f) {
    yaw -= 360.0f;
  }

  int hud_w = 340;
  int hud_h = 160;
  DrawRectangle(10, 10, hud_w, hud_h, Fade(BLACK, 0.75f));
  DrawRectangleLines(10, 10, hud_w, hud_h, DARKGRAY);

  if (ghost_mode) {
    DrawText("GHOST MODE: ON", 20, 18, 16, GREEN);
  } else {
    DrawText("GHOST MODE: OFF", 20, 18, 16, LIGHTGRAY);
  }
  DrawText("(Press 'I')", 180, 18, 14, GRAY);

  std::string pos_text = TextFormat("Pos: (%.2f, %.2f, %.2f)", p.x, p.y, p.z);
  DrawText(pos_text.c_str(), 20, 38, 15, WHITE);

  std::string yaw_text = TextFormat("Yaw: %.1f deg", yaw);
  DrawText(yaw_text.c_str(), 20, 56, 15, SKYBLUE);

  // Selected NPC status
  std::string npc_status = "None";
  if (selected_debug_npc != nullptr && !selected_debug_npc->getStats().isDead()) {
    npc_status = enemyTypeName(selected_debug_npc->getType());
  }
  std::string sel_text = TextFormat("[K] Selected NPC: %s", npc_status.c_str());
  DrawText(sel_text.c_str(), 20, 76, 14, SKYBLUE);

  // Patrol waypoints count
  std::string wp_text = TextFormat("[O] Add Patrol Point (Count: %d)",
                                   static_cast<int>(recorded_waypoints.size()));
  DrawText(wp_text.c_str(), 20, 96, 14, LIME);

  // Delete option
  DrawText("[N] Mark Selected for Delete", 20, 116, 14, Color{255, 120, 120, 255});

  // Save pos
  DrawText("[P] Save Player Pos | [F4] Log", 20, 136, 13, GOLD);

  if (saved_pos_toast_timer > 0.0f) {
    std::string toast_msg = "[Log] " + last_saved_pos_str;
    int toast_width = MeasureText(toast_msg.c_str(), 18);
    int toast_x = 10;
    int toast_y = 178;
    DrawRectangle(toast_x, toast_y, toast_width + 20, 30, Fade(BLACK, 0.85f));
    DrawRectangleLines(toast_x, toast_y, toast_width + 20, 30, GREEN);
    DrawText(toast_msg.c_str(), toast_x + 10, toast_y + 6, 18, GREEN);
  }
}

void GameplayState::drawEnemyOrientationArrows() const {
  for (const auto &enemy_ptr : enemies) {
    if (enemy_ptr->isModelUnloaded() || enemy_ptr->getStats().isDead()) {
      continue;
    }

    Vector3 pos = enemy_ptr->getPosition();
    pos.y += 1.0f;

    float yaw_rad = enemy_ptr->getRotation().y * DEG2RAD;
    Vector3 forward = {std::sin(yaw_rad), 0.0f, std::cos(yaw_rad)};

    float arrow_len = 1.5f;
    Vector3 end_pos = Vector3Add(pos, Vector3Scale(forward, arrow_len));

    DrawCylinderEx(pos, end_pos, 0.035f, 0.035f, 8, ORANGE);

    Vector3 tip_pos = Vector3Add(end_pos, Vector3Scale(forward, 0.35f));
    DrawCylinderEx(end_pos, tip_pos, 0.10f, 0.0f, 8, RED);
  }
}

void GameplayState::drawEnemyOverheadInfo() const {
  const Camera3D &camera = camera_controller->getCamera();
  Vector3 cam_forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));

  for (const auto &enemy_ptr : enemies) {
    if (enemy_ptr->isModelUnloaded() || enemy_ptr->getStats().isDead()) {
      continue;
    }

    const Vector3 pos = enemy_ptr->getPosition();
    float yaw = enemy_ptr->getRotation().y;
    while (yaw <= -180.0f) {
      yaw += 360.0f;
    }
    while (yaw > 180.0f) {
      yaw -= 360.0f;
    }

    Vector3 head_pos = {pos.x, pos.y + enemy_ptr->getColliderHeight() + 0.7f, pos.z};
    Vector3 to_enemy = Vector3Subtract(head_pos, camera.position);

    float dist_sq = Vector3LengthSqr(to_enemy);
    if (dist_sq > 40.0f * 40.0f) {
      continue;
    }

    if (Vector3DotProduct(cam_forward, to_enemy) <= 0.0f) {
      continue;
    }

    Vector2 screen_pos = GetWorldToScreen(head_pos, camera);
    if (screen_pos.x < 0 || screen_pos.x > GetScreenWidth() ||
        screen_pos.y < 0 || screen_pos.y > GetScreenHeight()) {
      continue;
    }

    bool is_selected = (enemy_ptr.get() == selected_debug_npc);
    bool is_marked_delete = false;
    for (const Enemy *marked : marked_deletion_npcs) {
      if (marked == enemy_ptr.get()) {
        is_marked_delete = true;
        break;
      }
    }

    std::string line_1 = TextFormat("%s (%.2f, %.2f, %.2f)",
                                    enemyTypeName(enemy_ptr->getType()),
                                    pos.x, pos.y, pos.z);
    if (is_selected) {
      line_1 = "[SELECTED] " + line_1;
    }
    if (is_marked_delete) {
      line_1 = "[DELETE] " + line_1;
    }

    std::string line_2 = TextFormat("Yaw: %.1f deg", yaw);

    int font_size = 12;
    int w1 = MeasureText(line_1.c_str(), font_size);
    int w2 = MeasureText(line_2.c_str(), font_size);
    int box_w = std::max(w1, w2) + 12;
    int box_h = 32;

    int box_x = static_cast<int>(screen_pos.x - (box_w * 0.5f));
    int box_y = static_cast<int>(screen_pos.y - box_h - 4);

    DrawRectangle(box_x, box_y, box_w, box_h, Fade(BLACK, 0.7f));
    if (is_marked_delete) {
      DrawRectangleLines(box_x, box_y, box_w, box_h, RED);
    } else if (is_selected) {
      DrawRectangleLines(box_x, box_y, box_w, box_h, SKYBLUE);
    } else {
      DrawRectangleLines(box_x, box_y, box_w, box_h, DARKGRAY);
    }

    Color header_color = YELLOW;
    if (is_marked_delete) {
      header_color = Color{255, 100, 100, 255};
    } else if (is_selected) {
      header_color = SKYBLUE;
    }

    DrawText(line_1.c_str(), box_x + 6, box_y + 4, font_size, header_color);
    DrawText(line_2.c_str(), box_x + 6, box_y + 18, font_size, SKYBLUE);
  }
}