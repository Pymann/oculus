#include <stddef.h>

int *element(int *base, size_t index)
{
    return base + index * sizeof(int);
}
