#pragma once

#include <vector>
#include <memory>
#include <functional>

namespace BT {

enum class NodeState {
    RUNNING,
    SUCCESS,
    FAILURE
};

// Base class for all nodes
class Node {
public:
    virtual ~Node() = default;
    virtual NodeState tick() = 0;
};

using NodePtr = std::shared_ptr<Node>;

// Sequence Node: Ticks children from left to right. Fails if one fails. Succeeds if all succeed.
class Sequence : public Node {
public:
    Sequence(std::vector<NodePtr> children) : children(std::move(children)) {}

    NodeState tick() override {
        for (auto& child : children) {
            NodeState state = child->tick();
            if (state != NodeState::SUCCESS) {
                return state;
            }
        }
        return NodeState::SUCCESS;
    }
private:
    std::vector<NodePtr> children;
};

// Selector Node: Ticks children from left to right. Succeeds if one succeeds. Fails if all fail.
class Selector : public Node {
public:
    Selector(std::vector<NodePtr> children) : children(std::move(children)) {}

    NodeState tick() override {
        for (auto& child : children) {
            NodeState state = child->tick();
            if (state != NodeState::FAILURE) {
                return state;
            }
        }
        return NodeState::FAILURE;
    }
private:
    std::vector<NodePtr> children;
};

// Action Node: Executes a lambda function.
class Action : public Node {
public:
    using ActionFunc = std::function<NodeState()>;
    Action(ActionFunc func) : func(std::move(func)) {}

    NodeState tick() override {
        if (func) return func();
        return NodeState::FAILURE;
    }
private:
    ActionFunc func;
};

// Condition Node: Evaluates a lambda function. Succeeds if true, Fails if false.
class Condition : public Node {
public:
    using ConditionFunc = std::function<bool()>;
    Condition(ConditionFunc func) : func(std::move(func)) {}

    NodeState tick() override {
        if (func && func()) return NodeState::SUCCESS;
        return NodeState::FAILURE;
    }
private:
    ConditionFunc func;
};

} // namespace BT
