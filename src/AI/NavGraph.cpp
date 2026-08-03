#include <AI/NavGraph.h>
#include <raymath.h>
#include <queue>
#include <limits>
#include <algorithm>

int NavGraph::addNode(Vector3 position) {
    int id = static_cast<int>(nodes.size());
    nodes.push_back({position, {}});
    return id;
}

void NavGraph::addUndirectedEdge(int a, int b) {
    if (a >= 0 && a < nodes.size() && b >= 0 && b < nodes.size()) {
        nodes[a].neighbors.push_back(b);
        nodes[b].neighbors.push_back(a);
    }
}

void NavGraph::addDirectedEdge(int from, int to) {
    if (from >= 0 && from < nodes.size() && to >= 0 && to < nodes.size()) {
        nodes[from].neighbors.push_back(to);
    }
}

int NavGraph::getClosestNode(Vector3 pos) const {
    if (nodes.empty()) return -1;
    
    int closest_id = -1;
    float min_dist = std::numeric_limits<float>::max();
    
    for (size_t i = 0; i < nodes.size(); ++i) {
        // Pseudo-distance: heavily penalize vertical difference 
        // to prevent picking nodes that are physically close but on different floors/stairs.
        float y_diff = std::abs(nodes[i].position.y - pos.y);
        float h_dist = Vector2Distance({nodes[i].position.x, nodes[i].position.z}, {pos.x, pos.z});
        
        float dist = h_dist + (y_diff * 10.0f);
        
        if (dist < min_dist) {
            min_dist = dist;
            closest_id = static_cast<int>(i);
        }
    }
    
    return closest_id;
}

std::vector<Vector3> NavGraph::findPath(Vector3 start, Vector3 end) const {
    std::vector<Vector3> path;
    int startId = getClosestNode(start);
    int endId = getClosestNode(end);
    
    if (startId == -1 || endId == -1) return path;
    
    if (startId == endId) {
        path.push_back(nodes[endId].position);
        return path;
    }

    struct AStarNode {
        int id;
        float fScore;
        bool operator>(const AStarNode& other) const {
            return fScore > other.fScore;
        }
    };

    std::priority_queue<AStarNode, std::vector<AStarNode>, std::greater<AStarNode>> openSet;
    std::vector<int> cameFrom(nodes.size(), -1);
    std::vector<float> gScore(nodes.size(), std::numeric_limits<float>::max());

    gScore[startId] = 0.0f;
    openSet.push({startId, Vector3Distance(nodes[startId].position, nodes[endId].position)});

    while (!openSet.empty()) {
        int current = openSet.top().id;
        openSet.pop();

        if (current == endId) {
            // Reconstruct path
            int curr = endId;
            while (curr != startId) {
                path.push_back(nodes[curr].position);
                curr = cameFrom[curr];
            }
            path.push_back(nodes[startId].position);
            std::reverse(path.begin(), path.end());
            return path;
        }

        for (int neighbor : nodes[current].neighbors) {
            float tentative_gScore = gScore[current] + Vector3Distance(nodes[current].position, nodes[neighbor].position);
            
            if (tentative_gScore < gScore[neighbor]) {
                cameFrom[neighbor] = current;
                gScore[neighbor] = tentative_gScore;
                float fScore = tentative_gScore + Vector3Distance(nodes[neighbor].position, nodes[endId].position);
                openSet.push({neighbor, fScore});
            }
        }
    }

    return path; // No path found
}
