#include <stdint.h>

uint32_t top_bit(void)
{
    uint32_t one = 1U;
    return one << 32U;
}
