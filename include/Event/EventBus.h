#pragma once
#include <vector>
#include <functional>

class EventBus {
public:
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    static EventBus& getInstance() {
        static EventBus instance;
        return instance;
    }

    template<typename T>
    void subscribe(std::function<void(const T&)> callback) {
        getHandlers<T>().push_back(callback);
    }

    template<typename T>
    void publish(const T& event) {
        for (const auto& callback : getHandlers<T>()) {
            callback(event);
        }
    }

private:
    EventBus() = default;

    template<typename T>
    static std::vector<std::function<void(const T&)>>& getHandlers() {
        static std::vector<std::function<void(const T&)>> handlers;
        return handlers;
    }
};