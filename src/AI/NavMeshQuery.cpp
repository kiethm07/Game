#include <AI/NavMeshQuery.h>
#include <DetourNavMeshQuery.h>
#include <DetourNavMesh.h>
#include <raymath.h>

NavMeshQuery::NavMeshQuery() : m_navQuery(nullptr), m_filter(nullptr) {}

NavMeshQuery::~NavMeshQuery() {
    if (m_navQuery) dtFreeNavMeshQuery(m_navQuery);
    if (m_filter) delete m_filter;
}

bool NavMeshQuery::init(dtNavMesh* navMesh) {
    if (!navMesh) return false;
    
    m_navQuery = dtAllocNavMeshQuery();
    if (!m_navQuery) return false;
    
    dtStatus status = m_navQuery->init(navMesh, 2048);
    if (dtStatusFailed(status)) return false;
    
    m_filter = new dtQueryFilter();
    m_filter->setIncludeFlags(1);
    m_filter->setExcludeFlags(0);
    
    return true;
}

std::vector<Vector3> NavMeshQuery::findPath(Vector3 start, Vector3 end) const {
    std::vector<Vector3> path;
    if (!m_navQuery) return path;

    float extents[3] = { 2.0f, 4.0f, 2.0f }; // Search box size
    
    float startPos[3] = { start.x, start.y, start.z };
    float endPos[3] = { end.x, end.y, end.z };
    
    dtPolyRef startRef;
    float nearestStart[3];
    m_navQuery->findNearestPoly(startPos, extents, m_filter, &startRef, nearestStart);
    
    dtPolyRef endRef;
    float nearestEnd[3];
    m_navQuery->findNearestPoly(endPos, extents, m_filter, &endRef, nearestEnd);
    
    if (!startRef || !endRef) return path;
    
    dtPolyRef pathPolys[256];
    int pathCount = 0;
    m_navQuery->findPath(startRef, endRef, nearestStart, nearestEnd, m_filter, pathPolys, &pathCount, 256);
    
    if (pathCount > 0) {
        float straightPath[256 * 3];
        unsigned char straightPathFlags[256];
        dtPolyRef straightPathPolys[256];
        int straightPathCount = 0;
        
        m_navQuery->findStraightPath(nearestStart, nearestEnd, pathPolys, pathCount,
                                     straightPath, straightPathFlags, straightPathPolys,
                                     &straightPathCount, 256, 0);
                                     
        for (int i = 0; i < straightPathCount; ++i) {
            path.push_back({ straightPath[i*3], straightPath[i*3+1], straightPath[i*3+2] });
        }
    }
    
    return path;
}

bool NavMeshQuery::raycast(Vector3 start, Vector3 end) const {
    if (!m_navQuery) return false;

    float extents[3] = { 2.0f, 4.0f, 2.0f };
    float startPos[3] = { start.x, start.y, start.z };
    float endPos[3] = { end.x, end.y, end.z };

    dtPolyRef startRef;
    m_navQuery->findNearestPoly(startPos, extents, m_filter, &startRef, 0);

    if (!startRef) return false;

    float t = 0.0f;
    float hitNormal[3];
    dtPolyRef path[16];
    int pathCount = 0;

    // Raycast along the NavMesh. t will be 1.0f if the ray reached endPos without hitting an edge.
    m_navQuery->raycast(startRef, startPos, endPos, m_filter, &t, hitNormal, path, &pathCount, 16);

    return (t >= 1.0f);
}

Vector3 NavMeshQuery::getConstrainedPosition(Vector3 start, Vector3 intendedEnd) const {
    if (!m_navQuery) return intendedEnd;

    float extents[3] = { 2.0f, 4.0f, 2.0f };
    float startPos[3] = { start.x, start.y, start.z };
    float endPos[3] = { intendedEnd.x, intendedEnd.y, intendedEnd.z };

    dtPolyRef startRef;
    float nearestStart[3];
    m_navQuery->findNearestPoly(startPos, extents, m_filter, &startRef, nearestStart);

    if (!startRef) return intendedEnd;

    float resultPos[3];
    dtPolyRef visited[16];
    int visitedCount = 0;

    m_navQuery->moveAlongSurface(startRef, nearestStart, endPos, m_filter, resultPos, visited, &visitedCount, 16);

    // Calculate the valid movement delta on the NavMesh
    Vector3 delta;
    delta.x = resultPos[0] - nearestStart[0];
    delta.y = resultPos[1] - nearestStart[1];
    delta.z = resultPos[2] - nearestStart[2];

    // Apply the pure movement delta to the original start position.
    // This prevents the character from being violently snapped to 'nearestStart'
    // if they are slightly off the NavMesh (e.g. pushed by PhysicsManager).
    return { start.x + delta.x, start.y + delta.y, start.z + delta.z };
}
