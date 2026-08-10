#pragma once
#include <vector>
#include <raylib.h>
#include <Components/PhysicsObstacle.h>
#include <Physics/CollisionMesh.h>

class dtNavMesh;

class NavMeshBuilder {
public:
    NavMeshBuilder();
    ~NavMeshBuilder();

    // Add a mesh to the builder's internal geometry collection
    void addMesh(const Mesh& mesh, Matrix transform);

    // Add a level's collision mesh.
    //
    // This is the preferred source where a level has one. Recast wants a
    // triangle soup and rasterizes it into a heightfield, so handing it the
    // real surface produces a better navmesh than handing it the boxes that
    // used to approximate that surface -- and it is now the only way the AI
    // gets a floor at all, since the castle's ground proxies were replaced by
    // its mesh.
    void addCollisionMesh(const CollisionMesh& mesh);
    
    // Add a PhysicsObstacle directly
    void addObstacle(const PhysicsObstacle& obs);

    // Build the navigation mesh from accumulated geometry
    bool build();

    // Get the generated NavMesh
    dtNavMesh* getNavMesh() const { return m_navMesh; }

private:
    struct GeometryData {
        std::vector<float> vertices;
        std::vector<int> triangles;
    };
    GeometryData m_geom;

    dtNavMesh* m_navMesh;
};
