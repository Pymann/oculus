#include <stdlib.h>

int *make(size_t count)
{
    int *values = malloc(count * sizeof *values);
    return values;
}
