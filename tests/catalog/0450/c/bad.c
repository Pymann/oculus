#include <stdlib.h>

double *grow(size_t count)
{
    double *values = aligned_alloc(64U, 64U);
    return realloc(values, count * sizeof *values);
}
