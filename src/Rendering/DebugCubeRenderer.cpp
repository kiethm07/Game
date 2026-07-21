#include <Rendering/DebugCubeRenderer.h>
#include <rlgl.h>

DebugCubeRenderer::DebugCubeRenderer(Color fillColor, Color wireColor)
    : fillColor(fillColor), wireColor(wireColor) {}

void DebugCubeRenderer::draw(AssetManager & /*assets*/,
                             const CharacterRenderData &renderData) {
    const Vector3 &pos   = renderData.transform.position;
    const Vector3 &scale = renderData.transform.scale;
    const float    yaw   = renderData.transform.rotation.y;

    rlPushMatrix();
    rlTranslatef(pos.x, pos.y, pos.z);
    rlRotatef(yaw, 0.0f, 1.0f, 0.0f);

    DrawCube({0.0f, 0.0f, 0.0f}, scale.x, scale.y, scale.z, fillColor);
    DrawCubeWires({0.0f, 0.0f, 0.0f}, scale.x, scale.y, scale.z, wireColor);

    rlPopMatrix();
}
