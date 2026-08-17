#include <Rendering/SwordTrail.h>
#include <raymath.h>
#include <rlgl.h>

namespace {
const size_t MAX_SEGMENTS = 64;
}

SwordTrail::SwordTrail()
    : inner_color({255, 255, 255, 240}),
      outer_color({80, 200, 255, 200}) {}

void SwordTrail::setColors(Color inner, Color outer) {
  inner_color = inner;
  outer_color = outer;
}

void SwordTrail::addSegment(Vector3 base_point, Vector3 tip_point, float duration) {
  if (points.size() >= MAX_SEGMENTS) {
    points.erase(points.begin());
  }
  points.emplace_back(base_point, tip_point, duration);
}

void SwordTrail::update(float dt) {
  for (size_t i = 0; i < points.size();) {
    points[i].updateLife(dt);
    if (points[i].isExpired()) {
      points.erase(points.begin() + i);
    } else {
      ++i;
    }
  }
}

void SwordTrail::clear() {
  points.clear();
}

void SwordTrail::draw() const {
  if (points.size() < 2) {
    return;
  }

  rlDisableBackfaceCulling();
  BeginBlendMode(BLEND_ADDITIVE);

  rlBegin(RL_TRIANGLES);

  for (size_t i = 0; i + 1 < points.size(); ++i) {
    float alpha_start = points[i].getCurrentLife() / points[i].getInitialLife();
    if (alpha_start < 0.0f) {
      alpha_start = 0.0f;
    }
    if (alpha_start > 1.0f) {
      alpha_start = 1.0f;
    }

    float alpha_end = points[i + 1].getCurrentLife() / points[i + 1].getInitialLife();
    if (alpha_end < 0.0f) {
      alpha_end = 0.0f;
    }
    if (alpha_end > 1.0f) {
      alpha_end = 1.0f;
    }

    unsigned char byte_inner_a = static_cast<unsigned char>(inner_color.a * alpha_start);
    unsigned char byte_outer_a = static_cast<unsigned char>(outer_color.a * alpha_start);
    unsigned char byte_inner_b = static_cast<unsigned char>(inner_color.a * alpha_end);
    unsigned char byte_outer_b = static_cast<unsigned char>(outer_color.a * alpha_end);

    Color col_base_a = {inner_color.r, inner_color.g, inner_color.b, byte_inner_a};
    Color col_tip_a = {outer_color.r, outer_color.g, outer_color.b, byte_outer_a};
    Color col_base_b = {inner_color.r, inner_color.g, inner_color.b, byte_inner_b};
    Color col_tip_b = {outer_color.r, outer_color.g, outer_color.b, byte_outer_b};

    Vector3 b_a = points[i].getBasePosition();
    Vector3 t_a = points[i].getTipPosition();
    Vector3 b_b = points[i + 1].getBasePosition();
    Vector3 t_b = points[i + 1].getTipPosition();

    // Triangle 1: b_a -> t_a -> t_b
    rlColor4ub(col_base_a.r, col_base_a.g, col_base_a.b, col_base_a.a);
    rlVertex3f(b_a.x, b_a.y, b_a.z);

    rlColor4ub(col_tip_a.r, col_tip_a.g, col_tip_a.b, col_tip_a.a);
    rlVertex3f(t_a.x, t_a.y, t_a.z);

    rlColor4ub(col_tip_b.r, col_tip_b.g, col_tip_b.b, col_tip_b.a);
    rlVertex3f(t_b.x, t_b.y, t_b.z);

    // Triangle 2: b_a -> t_b -> b_b
    rlColor4ub(col_base_a.r, col_base_a.g, col_base_a.b, col_base_a.a);
    rlVertex3f(b_a.x, b_a.y, b_a.z);

    rlColor4ub(col_tip_b.r, col_tip_b.g, col_tip_b.b, col_tip_b.a);
    rlVertex3f(t_b.x, t_b.y, t_b.z);

    rlColor4ub(col_base_b.r, col_base_b.g, col_base_b.b, col_base_b.a);
    rlVertex3f(b_b.x, b_b.y, b_b.z);
  }

  rlEnd();

  EndBlendMode();
  rlEnableBackfaceCulling();
}
