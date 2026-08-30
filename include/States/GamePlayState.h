#pragma once

#include <GameManager/StealthManager.h>
#include <GameManager/SmokeCloud.h>
#include <Rendering/ParticleManager.h>

#include <Components/PhysicsObstacle.h>
#include <Core/CameraController.h>
#include <Core/Game.h>
#include <Core/InputManager.h>
#include <Entities/Enemy.h>
#include <Entities/EnemyFactory.h>
#include <Entities/Player.h>
#include <GameManager/CombatManager.h>
#include <GameManager/PhysicsManager.h>
#include <AI/NavMeshBuilder.h>
#include <AI/NavMeshQuery.h>
#include <Level/Campaign.h>
#include <Level/Checkpoint.h>
#include <Level/Level.h>
#include <Physics/CollisionMesh.h>
#include <States/GameState.h>

#include <Rendering/GameRenderer.h>
#include <Rendering/RenderData.h>

#include <memory>
#include <vector>
#include <string>

#include <GameManager/SoundController.h>

struct MoneyDrop {
    Vector3 position;
    int amount;
    float bob_timer;
};

class GameplayState : public GameState {
public:
  GameplayState(const InputManager &input_manager, AssetManager &asset_manager,
                SoundController &sound_controller, Campaign &campaign);
  ~GameplayState() override;

  void enter() override;
  StateAction update(float dt) override;
  void draw() override;
  void exit() override;

private:
  /// The marker shown on a posture-broken enemy's chest, telling the player a
  /// deathblow is available. Loaded in enter() and freed in exit(), which is
  /// also why this state now has a destructor.
  ///
  /// Held here rather than in AssetManager for the same reason MainMenuState
  /// holds its background: AssetManager has no texture map at all, its manifest
  /// is a model/animation/sound list, and exactly one screen draws this.
  Texture2D posture_cue{};

  /// The boss still standing between the player and this phase's exit, or
  /// nullptr when there is none.
  ///
  /// A phase's campfire stays shut while any boss-type enemy in it is alive --
  /// which is what makes phase 2's mini boss a wall rather than an optional
  /// fight. Expressed over EnemyTypes' isBossType rather than over a per-phase
  /// list, so a phase that gains a boss is gated by the spawn alone and no
  /// table has to be kept in step with the level.
  ///
  /// Returns the enemy rather than a bool because the prompt names what is
  /// blocking it, and a bool cannot.
  const Enemy *blockingBoss() const;

  /// Which deathblow, if any, is on offer against one enemy right now.
  ///
  /// `None` is the answer to "nothing here", and the other three name the route
  /// taken, because the follow-through differs: an aerial drop is set up over
  /// several frames, a stealth kill aligns behind the target and dissolves it,
  /// a combat deathblow turns to face it.
  enum class TakedownKind { None, Aerial, Stealth, Combat };

  /// Is a deathblow available on `enemy`, and by which route?
  ///
  /// One implementation, deliberately, because two things ask: the Takedown
  /// input acts on it and the chest marker advertises it. Those two answering
  /// differently is the specific bug this shape prevents -- a marker over an
  /// enemy that F refuses, or an enemy F would kill wearing no marker.
  ///
  /// Per-enemy only. The global gates -- already executing, an aerial drop
  /// already committed -- belong to the callers, which is why they are not
  /// tested here.
  TakedownKind availableTakedown(const Enemy &enemy) const;

  /// One clickable row on the defeat screen: where it is, what it says, and
  /// what it asks Game to do.
  ///
  /// Carries the StateAction directly, unlike MainMenuState's button, and it
  /// can: both destinations here are ones this state already knows by name --
  /// the menu, and this same phase again. The menu's buttons carry an index
  /// instead precisely because their choice has to be written into Campaign
  /// before the action is returned, and there is no such choice to make here.
  struct DefeatButton {
    Rectangle bounds;
    const char *label;
    StateAction action;
  };

  /// Lay the two buttons out under the banner, from GetScreenWidth(). Called
  /// when the screen goes up rather than in the constructor, for the same
  /// reason MainMenuState builds its stack in enter().
  void buildDefeatButtons();

  /// The defeat screen's whole frame: hover, clicks, and the one key that also
  /// leaves. Returns what Game should do, exactly as update() does -- it is
  /// update(), for every frame after the screen appears.
  StateAction updateDefeatScreen();

  /// Grey wash, banner and buttons, over whatever the 3D pass last drew. Must
  /// be called after the 3D scope is closed.
  void drawDefeatScreen();

  /// Free `posture_cue` and zero the handle. Called from exit() and from the
  /// destructor; zeroing is what makes the second call a no-op.
  void unloadPostureCue();

  /// Draw the cue over every enemy currently open to a deathblow. Must be
  /// called inside a BeginMode3D scope.
  void drawPostureCues();

  /// The mini boss's and the final boss's posture, pinned to the top of the
  /// screen for as long as they are awake to the player.
  ///
  /// Here rather than on Enemy because the placement is the screen's, not the
  /// boss's: nothing about where a boss stands decides where its bar goes, and
  /// two of them awake at once have to be told apart by stacking, which no one
  /// boss can do alone. Must be called after the 3D scope is closed.
  void drawBossPostureBars();

  /// Declared before `renderer`, which holds a reference to it: destruction is
  /// reverse-declaration order, so the renderer dies first.
  AssetManager& asset_manager;

  /// The loaded map. Also declared before `renderer`, which reads its visual
  /// mesh path and bounds during construction.
  Level level;

  /// Triangle collision for the parts of the world BOX_/RAMP_ proxies cannot
  /// express. Empty for levels that ship none (greybox, forest), and every
  /// consumer has to tolerate that rather than assume a mesh is present.
  CollisionMesh collision_mesh;

  std::unique_ptr<CameraController> camera_controller;
  std::unique_ptr<Player> player;
  std::vector<std::unique_ptr<Enemy>> enemies;
  std::unique_ptr<GameRenderer> renderer;
  CombatManager combat_manager;
  PhysicsManager physics_manager;
  StealthManager stealth_manager;
  std::vector<SmokeCloud> smoke_clouds;
  std::vector<MoneyDrop> money_drops;
  ParticleManager particle_manager;
  std::vector<CharacterRenderData> renderList;

  const InputManager &input_manager;
  SoundController &sound_controller;

  /// Which phase this is, and the run's carry. Read in the constructor to
  /// choose the level and restore the player, written when the phase is left.
  /// A reference to a Game member, like the three above it -- states are
  /// destroyed before the services they point at.
  Campaign &campaign;

  /// The money and item charges this phase would hand to the next one.
  /// Read off the live Player, so only meaningful while one exists.
  PhaseCarry snapshotCarry() const;

  /// The F4 readout: a paste-ready enemies.json line, shown for a few seconds.
  /// Latched rather than drawn while a key is held, because the whole point is
  /// to press it and then go and read it somewhere else.
  std::string spawn_line;
  float spawn_line_timer = 0.0f;

  /// Which type F4 writes into that line. SHIFT+F4 cycles it.
  EnemyType debug_spawn_type = EnemyType::Swordman;

  /// Campfires in this level.
  ///
  /// Filled in the constructor from the campaign's exit for this phase, and
  /// deliberately NOT modelled into the map — a campfire baked into VISUAL is
  /// merged into a terrain chunk at export and could never be lit, because
  /// raylib's glTF loader discards mesh names and nothing would be left to
  /// address. F6 can still drop extra ones for testing.
  std::vector<Checkpoint> checkpoints;

  /// Counts down from CHECKPOINT_HOLD once a campfire is lit, then the phase
  /// ends. Negative means nothing is pending.
  ///
  /// The pause is the point: lighting the fire and being taken out of the level
  /// in the same frame reads as a glitch, whereas a beat of watching it catch
  /// reads as resting. It is also what makes the fire worth having — it is on
  /// screen, alight, for exactly as long as it takes to notice.
  float checkpoint_timer = -1.0f;

  /// Which campfire is burning down the clock, so the message can name it and
  /// a second G cannot start a second countdown.
  int pending_checkpoint = -1;

  /// True while the player is close enough to light one, so draw() can prompt.
  bool checkpoint_in_reach = false;

  /// The boss barring the campfire the player is standing at, or nullptr.
  ///
  /// Written by update() and read by draw(), so the prompt and the interaction
  /// can never disagree about whether the fire can be lit. Only meaningful when
  /// `checkpoint_in_reach`.
  const Enemy *checkpoint_blocked_by = nullptr;
  
  /// Seconds since the player ran out of health, or negative while they are
  /// alive. Started on the frame stats.isDead() first reads true, which is also
  /// the frame the fall begins -- Player::update takes the character over from
  /// the same test, so the clock and the animation start together rather than
  /// one being timed against the other.
  float death_timer = -1.0f;

  /// When the defeat screen is due, on that same clock: the fall clip's own
  /// playable length, so the banner lands on the frame the body settles.
  ///
  /// Measured off the clip rather than typed as a constant, and that is the
  /// whole of what holds this together now that there is no pause padding it.
  /// A hand-typed wait would have to be retyped every time the death animation
  /// changed, and would be wrong in one of two invisible ways in between --
  /// the banner over a fall still in progress, or a corpse held on screen after
  /// it stopped moving. An asset with no death clip reports 0, which puts the
  /// screen up on the frame of death: correct, since there is no fall to wait
  /// for.
  float defeat_at = 0.0f;

  /// True once the banner is up. The world stops being updated from here: the
  /// scene keeps being drawn, but nothing in it moves, so the grey wash sits
  /// over a still rather than over a fight that carries on without its player.
  bool defeat_shown = false;

  std::vector<DefeatButton> defeat_buttons;

  /// Index into `defeat_buttons`, or -1 for none.
  int defeat_hovered = -1;

  float takedown_text_timer = 0.0f;
  std::string takedown_type_str = "";
  float smoke_cooldown_timer = 0.0f;
  
  NavMeshBuilder nav_builder;
  NavMeshQuery nav_query;

  /// F1: overlay the raw shadow depth map in the corner.
  bool show_shadow_map = false;

  /// F9: hitbox (red) / hurtbox (green) wireframes from CombatManager::drawDebug.
  bool show_hitboxes = false;

  /// Used strictly for debugging takedown mechanics across frames.
  Character *pending_aerial_target = nullptr;

  /// Who is being executed, held for as long as the swing runs so the camera
  /// has a second point to compose its shot around. Cleared by the camera step
  /// once Player::isExecuting() goes false, so a victim outliving the animation
  /// is not possible. Safe to hold raw: enemies are owned by `enemies` for the
  /// whole life of the state and are never erased, only killed.
  Character *deathblow_victim = nullptr;
  Character *locked_target = nullptr;
};