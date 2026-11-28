#include <stdint.h>

uint32_t bit(uint32_t position)
{
    if (position < 32U) {
        return (uint32_t)1U << position;
    }
    return 0U;
}
