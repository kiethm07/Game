#include <Rendering/DebugCubeRenderer.h>
#include <rlgl.h>

DebugCubeRenderer::DebugCubeRenderer(Color fillColor, Color wireColor)
    : fillColor(fillColor), wireColor(wireColor) {}

void DebugCubeRenderer::draw(AssetManager & /*assets*/,
                             const CharacterRenderData &renderData,
                             RenderPass pass) {
  const Vector3 &pos = renderData.transform.position;
  const Vector3 &scale = renderData.transform.scale;
  const float yaw = renderData.transform.rotation.y;

  rlPushMatrix();
  rlTranslatef(pos.x, pos.y, pos.z);
  rlRotatef(yaw, 0.0f, 1.0f, 0.0f);

  // The proxy goes through rlgl immediate mode, so the shader GameRenderer has
  // bound for the pass applies as-is and the cube casts correctly. Only the
  // wire outline is pass-dependent: in the depth pass it would write thin
  // depth spikes along the silhouette for no benefit.
  DrawCube({0.0f, 0.0f, 0.0f}, scale.x, scale.y, scale.z, fillColor);
  if (pass == RenderPass::Scene) {
    DrawCubeWires({0.0f, 0.0f, 0.0f}, scale.x, scale.y, scale.z, wireColor);
  }

  rlPopMatrix();
}
