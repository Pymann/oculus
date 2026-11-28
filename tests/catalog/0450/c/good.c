#include <stdlib.h>
#include <string.h>

double *grow(size_t count)
{
    double *values = aligned_alloc(64U, 64U);
    double *bigger = aligned_alloc(64U, ((count * sizeof *values) + 63U) & ~(size_t)63U);
    if ((values != NULL) && (bigger != NULL)) {
        (void)memcpy(bigger, values, 64U);
    }
    free(values);
    return bigger;
}
