#pragma once
#include <vector>
#include <raylib.h>

struct PathNode {
    Vector3 position;
    std::vector<int> neighbors;
};

class NavGraph {
public:
    int addNode(Vector3 position);
    void addUndirectedEdge(int a, int b);
    void addDirectedEdge(int from, int to);

    int getClosestNode(Vector3 pos) const;
    std::vector<Vector3> findPath(Vector3 start, Vector3 end) const;

    const std::vector<PathNode>& getNodes() const { return nodes; }

private:
    std::vector<PathNode> nodes;
};
