#pragma once

#include <string>

/// Every kind of enemy the factory can build. Generated from EnemyTypes.def.
///
/// This header deliberately depends on nothing but <string>. It used to live in
/// EnemyFactory.h, which drags in Enemy.h and with it StealthComponent,
/// AIComponent, CombatComponent, InputManager, Character and SwordTrail -- so
/// every translation unit that merely mentioned a Level pulled the whole entity
/// system in for one enum. Level/EnemySpawn.h includes this instead.
enum class EnemyType {
#define ENEMY_TYPE(Name) Name,
#include <Entities/EnemyTypes.def>
#undef ENEMY_TYPE
};

/// Rows in EnemyTypes.def. Counted by the same expansion that built the enum,
/// so it cannot be off by one.
inline constexpr int kEnemyTypeCount = 0
#define ENEMY_TYPE(Name) + 1
#include <Entities/EnemyTypes.def>
#undef ENEMY_TYPE
    ;

/// The name this type carries in level.json and enemies.json.
/// Returns "<invalid>" for a value outside the table.
const char *enemyTypeName(EnemyType type);

/// Reverse. False when `name` matches no row, leaving `out` untouched.
bool parseEnemyType(const std::string &name, EnemyType &out);
