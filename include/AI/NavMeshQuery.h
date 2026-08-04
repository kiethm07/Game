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

private:
    dtNavMeshQuery* m_navQuery;
    dtQueryFilter* m_filter;
};
