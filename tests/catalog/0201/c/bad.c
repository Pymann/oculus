#include <string.h>

void copy_out(float *target, const int *source)
{
    (void)memcpy(target, source, sizeof *target);
}
