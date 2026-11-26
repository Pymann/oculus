#include <cstdint>

std::uintptr_t address(int *pointer)
{
    return reinterpret_cast<std::uintptr_t>(pointer);
}
