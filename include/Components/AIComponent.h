#pragma once

#include <AI/BehaviorTree.h>

class AIComponent {
public:
    AIComponent() = default;
    ~AIComponent() = default;

    // Set the root node of the behavior tree
    void setRoot(BT::NodePtr root_node) {
        root = std::move(root_node);
    }

    // Tick the behavior tree
    void update() {
        if (root) {
            root->tick();
        }
    }

private:
    BT::NodePtr root;
};
