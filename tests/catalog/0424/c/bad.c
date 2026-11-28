#include <stdint.h>

uint32_t read_word(const unsigned char *buffer)
{
    return *(const uint32_t *)(buffer + 1);
}
