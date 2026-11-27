#include <stdint.h>

static uint32_t state = 2463534242U;

uint32_t next_random(void)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
