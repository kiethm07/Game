#include <AI/NavMeshBuilder.h>
#include <raymath.h>
#include <Recast.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <iostream>
#include <cmath>
#include <cstring>

class BuildContext : public rcContext {
public:
    BuildContext() : rcContext(false) {}
    virtual void doLog(const rcLogCategory category, const char* msg, const int len) override {
        std::cout << "[Recast] " << msg << std::endl;
    }
};

NavMeshBuilder::NavMeshBuilder() : m_navMesh(nullptr) {
}

NavMeshBuilder::~NavMeshBuilder() {
    if (m_navMesh) {
        dtFreeNavMesh(m_navMesh);
    }
}

void NavMeshBuilder::addMesh(const Mesh& mesh, Matrix transform) {
    int startVertex = m_geom.vertices.size() / 3;
    
    // Add vertices
    for (int i = 0; i < mesh.vertexCount; ++i) {
        Vector3 v = { mesh.vertices[i*3], mesh.vertices[i*3+1], mesh.vertices[i*3+2] };
        Vector3 vt = Vector3Transform(v, transform);
        m_geom.vertices.push_back(vt.x);
        m_geom.vertices.push_back(vt.y);
        m_geom.vertices.push_back(vt.z);
    }
    
    // Add indices
    if (mesh.indices) {
        for (int i = 0; i < mesh.triangleCount * 3; ++i) {
            m_geom.triangles.push_back(startVertex + mesh.indices[i]);
        }
    } else {
        for (int i = 0; i < mesh.triangleCount * 3; ++i) {
            m_geom.triangles.push_back(startVertex + i);
        }
    }
}

void NavMeshBuilder::addObstacle(const PhysicsObstacle& obs) {
    Matrix localToWorld = obs.getLocalToWorld();
    int startVertex = m_geom.vertices.size() / 3;

    if (obs.getShape() == ObstacleShape::BOX_SHAPE) {
        BoundingBox local = obs.getLocalBox();
        Vector3 corners[8] = {
            { local.min.x, local.min.y, local.min.z },
            { local.max.x, local.min.y, local.min.z },
            { local.max.x, local.min.y, local.max.z },
            { local.min.x, local.min.y, local.max.z },
            { local.min.x, local.max.y, local.min.z },
            { local.max.x, local.max.y, local.min.z },
            { local.max.x, local.max.y, local.max.z },
            { local.min.x, local.max.y, local.max.z }
        };
        for (int i = 0; i < 8; ++i) {
            Vector3 world_corner = Vector3Transform(corners[i], localToWorld);
            m_geom.vertices.push_back(world_corner.x);
            m_geom.vertices.push_back(world_corner.y);
            m_geom.vertices.push_back(world_corner.z);
        }

        // Triangles (CCW order)
        int tris[] = {
            // Bottom (0,1,2,3)
            0, 1, 2,   0, 2, 3,
            // Top (4,5,6,7)
            4, 6, 5,   4, 7, 6,
            // Front (0,1,5,4)
            0, 5, 1,   0, 4, 5,
            // Right (1,2,6,5)
            1, 6, 2,   1, 5, 6,
            // Back (2,3,7,6)
            2, 7, 3,   2, 6, 7,
            // Left (3,0,4,7)
            3, 4, 0,   3, 7, 4
        };
        for (int i = 0; i < 36; ++i) {
            m_geom.triangles.push_back(startVertex + tris[i]);
        }
    } else {
        // RAMP_SHAPE (from PhysicsObstacle.cpp draw logic)
        BoundingBox local = obs.getLocalBox();
        // Since ramp_start_y, etc., are private, we can use getHeightAt (approx via getting corners)
        // Actually, we can just use the known AABB footprint and probe getHeightAt in local space.
        // Or wait, PhysicsObstacle has `getHeightAt` which works for WORLD positions. 
        // We can just construct the 8 points using the local_box footprint and `getHeightAt`.
        // A Ramp's local_box covers the XZ footprint exactly.
        float minX = local.min.x;
        float maxX = local.max.x;
        float minZ = local.min.z;
        float maxZ = local.max.z;
        
        // Drop the bottom by 10 meters to prevent NavMesh from routing under or into the side of the ramp
        float dropped_y = local.min.y - 10.0f;
        Vector3 b0_l = { minX, dropped_y, minZ };
        Vector3 b1_l = { maxX, dropped_y, minZ };
        Vector3 b2_l = { maxX, dropped_y, maxZ };
        Vector3 b3_l = { minX, dropped_y, maxZ };
        
        Vector3 b0_w = Vector3Transform(b0_l, localToWorld);
        Vector3 b1_w = Vector3Transform(b1_l, localToWorld);
        Vector3 b2_w = Vector3Transform(b2_l, localToWorld);
        Vector3 b3_w = Vector3Transform(b3_l, localToWorld);
        
        Vector3 t0_w = { b0_w.x, obs.getHeightAt(b0_w), b0_w.z };
        Vector3 t1_w = { b1_w.x, obs.getHeightAt(b1_w), b1_w.z };
        Vector3 t2_w = { b2_w.x, obs.getHeightAt(b2_w), b2_w.z };
        Vector3 t3_w = { b3_w.x, obs.getHeightAt(b3_w), b3_w.z };
        
        Vector3 corners[8] = { b0_w, b1_w, b2_w, b3_w, t0_w, t1_w, t2_w, t3_w };
        for (int i = 0; i < 8; ++i) {
            m_geom.vertices.push_back(corners[i].x);
            m_geom.vertices.push_back(corners[i].y);
            m_geom.vertices.push_back(corners[i].z);
        }
        
        int tris[] = {
            // Bottom (0,1,2,3)
            0, 1, 2,   0, 2, 3,
            // Top (4,5,6,7)
            4, 6, 5,   4, 7, 6,
            // Front (0,1,5,4)
            0, 5, 1,   0, 4, 5,
            // Right (1,2,6,5)
            1, 6, 2,   1, 5, 6,
            // Back (2,3,7,6)
            2, 7, 3,   2, 6, 7,
            // Left (3,0,4,7)
            3, 4, 0,   3, 7, 4
        };
        for (int i = 0; i < 36; ++i) {
            m_geom.triangles.push_back(startVertex + tris[i]);
        }

        // Add 4 guardrail vertices at the top to prevent stepping onto the ramp from the sides
        Vector3 guard_corners[4] = {
            { t0_w.x, t0_w.y + 2.0f, t0_w.z },
            { t1_w.x, t1_w.y + 2.0f, t1_w.z },
            { t2_w.x, t2_w.y + 2.0f, t2_w.z },
            { t3_w.x, t3_w.y + 2.0f, t3_w.z }
        };
        for (int i = 0; i < 4; ++i) {
            m_geom.vertices.push_back(guard_corners[i].x);
            m_geom.vertices.push_back(guard_corners[i].y);
            m_geom.vertices.push_back(guard_corners[i].z);
        }
        
        bool slopes_z = std::abs(t3_w.y - t0_w.y) > 0.01f;
        if (slopes_z) {
            // Left face: t0(4), t3(7), g3(11), g0(8)
            int left_tris[] = { 4, 7, 11, 4, 11, 8 };
            // Right face: t1(5), t2(6), g2(10), g1(9)
            int right_tris[] = { 5, 10, 6, 5, 9, 10 };
            for (int i = 0; i < 6; ++i) m_geom.triangles.push_back(startVertex + left_tris[i]);
            for (int i = 0; i < 6; ++i) m_geom.triangles.push_back(startVertex + right_tris[i]);
        } else {
            // Front face: t0(4), t1(5), g1(9), g0(8)
            int front_tris[] = { 4, 5, 9, 4, 9, 8 };
            // Back face: t3(7), t2(6), g2(10), g3(11)
            int back_tris[] = { 7, 10, 6, 7, 11, 10 };
            for (int i = 0; i < 6; ++i) m_geom.triangles.push_back(startVertex + front_tris[i]);
            for (int i = 0; i < 6; ++i) m_geom.triangles.push_back(startVertex + back_tris[i]);
        }
    }
}

bool NavMeshBuilder::build() {
    if (m_geom.vertices.empty() || m_geom.triangles.empty()) return false;
    
    BuildContext ctx;
    
    rcConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.cs = 0.3f; // Cell size
    cfg.ch = 0.2f; // Cell height
    cfg.walkableSlopeAngle = 60.0f; // Matches Physics COS_MAX_SLOPE
    cfg.walkableHeight = (int)std::ceil(2.0f / cfg.ch); // Agent height ~2m
    cfg.walkableClimb = (int)std::floor(0.9f / cfg.ch); // Agent max climb ~0.9m
    cfg.walkableRadius = (int)std::ceil(0.6f / cfg.cs); // Agent radius 0.5m + 0.1m safety margin
    cfg.maxEdgeLen = (int)(12.0f / cfg.cs);
    cfg.maxSimplificationError = 1.3f;
    cfg.minRegionArea = (int)rcSqr(8.0f);
    cfg.mergeRegionArea = (int)rcSqr(20.0f);
    cfg.maxVertsPerPoly = 6;
    cfg.detailSampleDist = 6.0f < 0.9f ? 0 : cfg.cs * 6.0f;
    cfg.detailSampleMaxError = cfg.ch * 1.0f;

    rcCalcBounds(m_geom.vertices.data(), m_geom.vertices.size() / 3, cfg.bmin, cfg.bmax);
    rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);

    rcHeightfield* solid = rcAllocHeightfield();
    if (!solid || !rcCreateHeightfield(&ctx, *solid, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch)) return false;

    std::vector<unsigned char> triareas(m_geom.triangles.size() / 3, 0);
    rcMarkWalkableTriangles(&ctx, cfg.walkableSlopeAngle, m_geom.vertices.data(), m_geom.vertices.size() / 3, m_geom.triangles.data(), m_geom.triangles.size() / 3, triareas.data());
    
    if (!rcRasterizeTriangles(&ctx, m_geom.vertices.data(), m_geom.vertices.size() / 3, m_geom.triangles.data(), triareas.data(), m_geom.triangles.size() / 3, *solid, cfg.walkableClimb)) return false;

    rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *solid);
    rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid);
    rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *solid);

    rcCompactHeightfield* chf = rcAllocCompactHeightfield();
    if (!chf || !rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid, *chf)) return false;
    rcFreeHeightField(solid);

    if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf)) return false;
    if (!rcBuildDistanceField(&ctx, *chf)) return false;
    if (!rcBuildRegions(&ctx, *chf, 0, cfg.minRegionArea, cfg.mergeRegionArea)) return false;

    rcContourSet* cset = rcAllocContourSet();
    if (!cset || !rcBuildContours(&ctx, *chf, cfg.maxSimplificationError, cfg.maxEdgeLen, *cset)) return false;

    rcPolyMesh* pmesh = rcAllocPolyMesh();
    if (!pmesh || !rcBuildPolyMesh(&ctx, *cset, cfg.maxVertsPerPoly, *pmesh)) return false;

    rcPolyMeshDetail* dmesh = rcAllocPolyMeshDetail();
    if (!dmesh || !rcBuildPolyMeshDetail(&ctx, *pmesh, *chf, cfg.detailSampleDist, cfg.detailSampleMaxError, *dmesh)) return false;
    
    rcFreeCompactHeightfield(chf);
    rcFreeContourSet(cset);

    for (int i = 0; i < pmesh->npolys; ++i) {
        if (pmesh->areas[i] == RC_WALKABLE_AREA) {
            pmesh->areas[i] = 0;
            pmesh->flags[i] = 1;
        }
    }

    dtNavMeshCreateParams params;
    memset(&params, 0, sizeof(params));
    params.verts = pmesh->verts;
    params.vertCount = pmesh->nverts;
    params.polys = pmesh->polys;
    params.polyAreas = pmesh->areas;
    params.polyFlags = pmesh->flags;
    params.polyCount = pmesh->npolys;
    params.nvp = pmesh->nvp;
    params.detailMeshes = dmesh->meshes;
    params.detailVerts = dmesh->verts;
    params.detailVertsCount = dmesh->nverts;
    params.detailTris = dmesh->tris;
    params.detailTriCount = dmesh->ntris;
    params.walkableHeight = 2.0f;
    params.walkableRadius = 0.6f;
    params.walkableClimb = 0.9f;
    params.bmin[0] = pmesh->bmin[0];
    params.bmin[1] = pmesh->bmin[1];
    params.bmin[2] = pmesh->bmin[2];
    params.bmax[0] = pmesh->bmax[0];
    params.bmax[1] = pmesh->bmax[1];
    params.bmax[2] = pmesh->bmax[2];
    params.cs = cfg.cs;
    params.ch = cfg.ch;
    params.buildBvTree = true;

    unsigned char* navData = 0;
    int navDataSize = 0;
    if (!dtCreateNavMeshData(&params, &navData, &navDataSize)) {
        rcFreePolyMesh(pmesh);
        rcFreePolyMeshDetail(dmesh);
        return false;
    }

    m_navMesh = dtAllocNavMesh();
    if (!m_navMesh) {
        dtFree(navData);
        rcFreePolyMesh(pmesh);
        rcFreePolyMeshDetail(dmesh);
        return false;
    }

    dtStatus status = m_navMesh->init(navData, navDataSize, DT_TILE_FREE_DATA);
    if (dtStatusFailed(status)) {
        dtFree(navData);
        rcFreePolyMesh(pmesh);
        rcFreePolyMeshDetail(dmesh);
        return false;
    }

    rcFreePolyMesh(pmesh);
    rcFreePolyMeshDetail(dmesh);

    return true;
}
