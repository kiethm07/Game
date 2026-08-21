#include <Entities/EnemyTypes.h>

namespace {
struct Row {
  const char *name;
  EnemyType type;
};

// The same rows the enum was built from, so a name and its enumerator cannot
// disagree -- they are the same token.
constexpr Row kRows[] = {
#define ENEMY_TYPE(Name) {#Name, EnemyType::Name},
#include <Entities/EnemyTypes.def>
#undef ENEMY_TYPE
};
} // namespace

const char *enemyTypeName(EnemyType type) {
  for (const Row &row : kRows) {
    if (row.type == type) return row.name;
  }
  return "<invalid>";
}

bool parseEnemyType(const std::string &name, EnemyType &out) {
  for (const Row &row : kRows) {
    if (name == row.name) {
      out = row.type;
      return true;
    }
  }
  return false;
}
