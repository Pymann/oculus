#include <memory>

class Engine {
public:
    ~Engine() = default;
};

void stop(Engine *engine)
{
    delete engine;
}

class Car {
public:
    Car();
    ~Car();   /* defined below, where Engine is complete */
private:
    std::unique_ptr<Engine> engine_;
};

Car::Car() : engine_(new Engine()) {}
Car::~Car() = default;
