#pragma once
#include <vector>
#include <raylib.h>
#include <Components/PhysicsObstacle.h>

class dtNavMesh;

class NavMeshBuilder {
public:
    NavMeshBuilder();
    ~NavMeshBuilder();

    // Add a mesh to the builder's internal geometry collection
    void addMesh(const Mesh& mesh, Matrix transform);
    
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
