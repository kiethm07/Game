#pragma once
#include <vector>
#include <raylib.h>

class dtNavMeshQuery;
class dtNavMesh;
class dtQueryFilter;

class NavMeshQuery {
public:
    NavMeshQuery();
    ~NavMeshQuery();

    bool init(dtNavMesh* navMesh);

    // Returns a smooth path from start to end
    std::vector<Vector3> findPath(Vector3 start, Vector3 end) const;

    // Raycasts along the NavMesh to check if there is a clear walkable path (no gaps/walls).
    // Optionally outputs the hit distance fraction and edge normal pointing into the mesh.
    bool raycast(Vector3 start, Vector3 end, float* hit_t = nullptr, Vector3* hit_normal = nullptr) const;

    // Constrains an intended movement destination to the NavMesh boundaries
    Vector3 getConstrainedPosition(Vector3 start, Vector3 intendedEnd) const;

private:
    dtNavMeshQuery* m_navQuery;
    dtQueryFilter* m_filter;
};
