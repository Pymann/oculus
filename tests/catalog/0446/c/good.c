#include <limits.h>

unsigned int high_bit(void)
{
    return (UINT_MAX >> 1) + 1U;
}
