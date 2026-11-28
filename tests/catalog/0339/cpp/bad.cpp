#include <functional>

std::function<int()> make(int base)
{
    int offset = 3;
    return [=] { return base + offset; };
}
