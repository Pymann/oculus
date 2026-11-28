#include <stddef.h>

int *element(int *base, size_t index)
{
    return &base[index];
}

unsigned char *byte_at(unsigned char *base, size_t index)
{
    return base + index * sizeof(int);   /* byte pointer: scaling is needed */
}
