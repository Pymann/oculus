#include <typeinfo>

struct Shape {
    virtual ~Shape() = default;
    virtual const char *name() const { return "shape"; }
};

struct Point {
    int x;
};

const char *name(const Shape &shape, const Point &point)
{
    static_cast<void>(typeid(Shape));
    static_cast<void>(typeid(point));
    return shape.name();
}
