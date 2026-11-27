#include <cstddef>
#include <new>

namespace app {
int scale(int value)
{
    return value * 2;
}
}

extern "C" int c_entry(int value);

void *operator new(std::size_t size);

int main()
{
    return app::scale(0);
}
