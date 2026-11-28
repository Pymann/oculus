#include <limits.h>

unsigned int high_bit(void)
{
    return 1U << (sizeof(unsigned int) * CHAR_BIT - 1U);
}
