#pragma once
#include <string>

class Player;

class Item {
public:
    Item(const std::string& name, int initial_count, float use_duration)
        : name(name), count(initial_count), use_duration(use_duration) {}
    
    virtual ~Item() = default;

    virtual void use(Player* player) = 0;

    std::string getName() const { return name; }
    int getCount() const { return count; }
    float getUseDuration() const { return use_duration; }
    
    bool isEmpty() const { return count <= 0; }
    
    void consume() {
        if (count > 0) {
            count--;
        }
    }
    
    void add(int amount) {
        count += amount;
    }

protected:
    std::string name;
    int count;
    float use_duration;
};
