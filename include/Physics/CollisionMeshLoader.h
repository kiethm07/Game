#pragma once

#include <Physics/CollisionMesh.h>

#include <string>

namespace CollisionMeshLoader {

/// Read a collision.bin written by tools/export_level.py into `out`.
///
/// Never throws. On any problem it logs the file and what was wrong, leaves
/// `out` empty and returns false — a level then collides against its BOX_/RAMP_
/// proxies alone, which is a degraded level rather than a crashed one.
///
/// The format is deliberately not glTF. raylib's Mesh indexes vertices with
/// `unsigned short`, and the castle's collision mesh has 45,612 vertices in one
/// piece; loading it through raylib would truncate indices to 16 bits and hand
/// back a mesh whose triangles join the wrong vertices. Collision that is subtly
/// wrong is worse than collision that is absent, so the payload is a flat
/// little-endian soup this reads directly. See export_collision_mesh().
bool load(const std::string &path, CollisionMesh &out);

} // namespace CollisionMeshLoader
