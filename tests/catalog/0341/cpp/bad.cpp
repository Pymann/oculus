#include <typeinfo>

struct Shape {
    virtual ~Shape() = default;
};

const char *name(const Shape &shape)
{
    return typeid(shape).name();
}
