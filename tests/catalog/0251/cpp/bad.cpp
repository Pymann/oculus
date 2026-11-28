#include <cstdint>

bool all_clear(std::uint8_t mask)
{
    return (~mask) == 0U;
}
