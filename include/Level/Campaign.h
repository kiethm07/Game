#pragma once

#include <Level/CampaignManifest.h>
#include <cstddef>
#include <vector>

/// What the player keeps across a phase boundary.
///
/// A seam is a rest point: money and item charges carry, health and posture do
/// not. "Do not" is free rather than enforced — each phase builds a fresh
/// Player, whose constructor sets Stats(1000, 100, 15) with current_health at
/// max and posture at zero. There is deliberately NO health field in here, so
/// that carrying damage across a seam is something someone would have to add on
/// purpose rather than something a later edit can do by accident.
///
/// Three things are also left behind on purpose. Uncollected MoneyDrops still
/// on the ground, because walking out on them is what makes leaving a decision.
/// The active item index, which is cosmetic and would only ever be restored to
/// the same fixed inventory. And the camera, because every level.json names its
/// own spawn yaw.
struct PhaseCarry {
  /// Player::getMoney() at the moment the phase was left.
  int money = 0;

  /// One entry per inventory slot, in Player's construction order
  /// (HealingGourd, SmokeBomb). Positional rather than keyed by name because
  /// Player's constructor builds those two unconditionally and in that order;
  /// Player::restoreItemCounts bounds the apply by both sizes so a snapshot
  /// taken before a third item existed still lands cleanly on the two that do.
  std::vector<int> itemCounts;
};

/// Where a run is, and what it is carrying.
///
/// Owned by Game and handed to the states that need it as a constructor
/// reference, exactly as AssetManager and SoundController are. That is what
/// lets a phase change happen without giving GameState a Game back-pointer: a
/// state still cannot touch the state stack. It writes its carry in here,
/// returns a StateAction by value, and Game does the rest after that state's
/// update() has already returned.
///
/// Holds no Level and performs no I/O. It answers "which file" and "how far
/// in". Loading stays in GameplayState's constructor, where it already was.
class Campaign {
public:
  Campaign() = default;

  /// The current phase's level.json. Never null — the cursor only ever moves
  /// through advance(), which refuses to leave the table.
  const char *currentLevelPath() const {
    return kCampaignPhases[cursor].levelPath;
  }
  const char *currentName() const { return kCampaignPhases[cursor].name; }

  size_t index() const { return cursor; }
  size_t count() const {
    return sizeof(kCampaignPhases) / sizeof(kCampaignPhases[0]);
  }

  /// Is there a phase after this one? Asked by Game::update to choose between
  /// loading the next one and ending the run.
  bool hasNext() const { return cursor + 1 < count(); }

  /// Move to the next phase. Returns false and changes nothing at the end, so
  /// one call both advances and answers.
  ///
  /// Touches the cursor only, never the carry: the carry was written by the
  /// phase being left, earlier in the same Game::update(), and clearing it here
  /// would wipe the snapshot on its way past.
  ///
  /// Note this runs a full loading screen before the new level is opened, so
  /// the cursor is advanced for about a second before anything reads it. That
  /// is deliberate rather than overlooked. Nothing in that window can observe
  /// the difference — LoadingState knows nothing about phases — and leaving it
  /// advanced is the recoverable behaviour: a phase whose level.json is broken
  /// can be skipped past with another press rather than trapping the run.
  bool advance() {
    if (!hasNext()) return false;
    ++cursor;
    return true;
  }

  /// Back to the first phase carrying nothing. This is what ends a run, and it
  /// is reached two ways: quitting out of a phase, and finishing the last one.
  void restart() {
    cursor = 0;
    carry = PhaseCarry{};
  }

  const PhaseCarry &getCarry() const { return carry; }
  void setCarry(const PhaseCarry &c) { carry = c; }

private:
  size_t cursor = 0;
  PhaseCarry carry{};
};
