#pragma once
#include <raylib.h>
#include <vector>

class TrailPoint {
public:
  TrailPoint(Vector3 base, Vector3 tip, float life)
      : base_pos(base), tip_pos(tip), current_life(life), initial_life(life) {}
  ~TrailPoint() = default;

  Vector3 getBasePosition() const { return base_pos; }
  Vector3 getTipPosition() const { return tip_pos; }
  float getCurrentLife() const { return current_life; }
  float getInitialLife() const { return initial_life; }

  void updateLife(float dt) { current_life -= dt; }

  bool isExpired() const { return current_life <= 0.0f; }

private:
  Vector3 base_pos;
  Vector3 tip_pos;
  float current_life;
  float initial_life;
};

class SwordTrail {
public:
  SwordTrail();
  ~SwordTrail() = default;

  void addSegment(Vector3 base_point, Vector3 tip_point, float duration = 0.20f);
  void update(float dt);
  void draw() const;
  void clear();

  void setColors(Color inner, Color outer);

private:
  std::vector<TrailPoint> points;
  Color inner_color;
  Color outer_color;
};
