#include <cstdint>

bool all_clear(std::uint8_t mask)
{
    return static_cast<std::uint8_t>(~mask) == 0U;
}
