#include <Entities/Player.h>
#include <cmath>
#include <raymath.h>
#include <rlgl.h>

Player::Player(const InputManager &input_manager)
    : Character(Faction::Player), input_manager(input_manager) {
  combo = {AttackID::PlayerLight1, AttackID::PlayerLight2};
  position = {0, 0, 0};
  rotation = {0, 180.0f, 0};
}

void Player::update(const UpdateContext &ctx) {
  const float dt = ctx.dt;
  // Advance framerate-independent playback time.
  animTime += dt;

  // Update combat component
  combat_component.update(dt);
  // Update stats component
  stats.update(dt);

  handleCombatAndUtilityInputs();

  Vector3 moveDirection =
      calculateCameraRelativeDirection(ctx.camForward, ctx.camRight);

  if (!combat_component.canMove()) {
    // Gated (e.g. mid-attack): request no movement. Physics still applies
    // gravity/collisions this frame.
    setHorizontalVelocity({0.0f, 0.0f, 0.0f});
    return;
  }

  // Produce desired velocity + ease facing; PhysicsManager integrates position.
  Vector3 velocity = movement_component.resolve(moveDirection, rotation.y, dt);
  setHorizontalVelocity(velocity);

  bool isMoving = (velocity.x != 0.0f || velocity.z != 0.0f);
  int targetAnimIndex = isMoving ? static_cast<int>(Player::AnimState::WALK)
                                 : static_cast<int>(Player::AnimState::IDLE);

  if (currentAnimIndex != targetAnimIndex) {
    currentAnimIndex = targetAnimIndex;
    animTime = 0.0f; // Reset animation when transitioning
  }
}

void Player::drawHPBar2D() const {
  float bar_width = 200.0f;
  float bar_height = 16.0f;

  int x = 20;
  int y = GetScreenHeight() - 40;

  float fill = stats.getHealthPercentage();

  DrawRectangle(x, y, (int)bar_width, (int)bar_height, DARKGRAY);
  DrawRectangle(x, y, (int)(bar_width * fill), (int)bar_height, LIME);
  DrawRectangleLines(x, y, (int)bar_width, (int)bar_height, WHITE);
}

float Player::getColliderRadius() const { return BODY_RADIUS; }

float Player::getColliderHeight() const { return BODY_HEIGHT; }

std::vector<HurtBox> Player::getHurtBoxes() const {
  // Generate an upright 3D body capsule centered at position
  Capsule body_capsule =
      Capsule::createUpright(position, BODY_HEIGHT, BODY_RADIUS);
  return {HurtBox(body_capsule, getFaction(), getId())};
}

std::vector<HitBox> Player::getActiveHitBoxes() const {
  std::vector<HitBox> active_hitboxes;

  if (combat_component.getCurrentState() == CombatState::AttackActive) {
    float yaw_rad = rotation.y * DEG2RAD;
    Vector3 forward = {std::sin(yaw_rad), 0.0f, std::cos(yaw_rad)};

    // Position attack sphere in front of the player at chest height
    Vector3 hitbox_center = {position.x + forward.x * ATTACK_REACH,
                             position.y + (BODY_HEIGHT * 0.5f),
                             position.z + forward.z * ATTACK_REACH};

    Sphere attack_sphere(hitbox_center, ATTACK_RADIUS);

    active_hitboxes.emplace_back(attack_sphere,
                                 25.0f, // Health damage
                                 15.0f, // Posture damage
                                 getFaction(), getId());
  }

  return active_hitboxes;
}

void Player::takeDamage(float health_damage, float posture_damage) {
  // 1. Guard check state machine windows
  if (combat_component.getCurrentState() == CombatState::Parrying) {
    // Perfect deflect window: Ignore damage entirely!
    return;
  }

  if (combat_component.getCurrentState() == CombatState::Blocking) {
    // Blocking cuts HP damage in half, but takes full posture damage
    health_damage = 0.0f;
  }

  // 2. Pass straight to Stats!
  bool hit_applied = stats.applyDamage(health_damage, posture_damage);

  if (hit_applied) {
    if (stats.isPostureBroken()) {
      // Stance broken state!
    } else if (stats.isDead()) {
      // Player death state!
    }
  }
}

Vector3 Player::calculateCameraRelativeDirection(Vector3 camForward,
                                                 Vector3 camRight) const {
  Vector3 direction = {0.0f, 0.0f, 0.0f};
  if (input_manager.isActionHeld(GameAction::MoveForward))
    direction = Vector3Add(direction, camForward);
  if (input_manager.isActionHeld(GameAction::MoveBackward))
    direction = Vector3Subtract(direction, camForward);
  if (input_manager.isActionHeld(GameAction::MoveRight))
    direction = Vector3Add(direction, camRight);
  if (input_manager.isActionHeld(GameAction::MoveLeft))
    direction = Vector3Subtract(direction, camRight);

  if (direction.x != 0.0f || direction.z != 0.0f) {
    direction = Vector3Normalize(direction);
  }
  return direction;
}

void Player::handleCombatAndUtilityInputs() {
  if (input_manager.isActionPressed(GameAction::Attack)) {
    combat_component.initiateCombo(combo);
  }
  if (input_manager.isActionPressed(GameAction::Parry)) {
    combat_component.startGuard();
  }
  if (input_manager.isActionReleased(GameAction::Parry)) {
    combat_component.stopGuard();
  }
  if (input_manager.isActionPressed(GameAction::Dodge)) {
  }
  if (input_manager.isActionPressed(GameAction::LockOn)) {
  }
}

CharacterRenderData Player::getRenderData() const {
  TransformData transform;
  transform.position = position;
  transform.rotation = rotation;
  transform.scale = {1.0f, 1.0f, 1.0f};

  AnimationState anim_state;
  anim_state.animIndex = currentAnimIndex;
  anim_state.animTime = animTime;

  return {AssetID::PLAYER_WOLF, transform, anim_state};
}