#include <cstring>

struct Point {
    int x;
    int y;
};

void copy(Point &target, const Point &source)
{
    std::memcpy(&target, &source, sizeof target);
}
