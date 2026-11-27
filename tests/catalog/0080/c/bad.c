#include <stdint.h>

uint32_t as_word(const float *value)
{
    return *(const uint32_t *)value;
}
