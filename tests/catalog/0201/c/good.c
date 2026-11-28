#include <string.h>

void copy_out(int *target, const int *source)
{
    (void)memcpy(target, source, sizeof *target);
}

void dump(unsigned char *bytes, const double *value)
{
    (void)memcpy(bytes, value, sizeof *value);   /* byte view is fine */
}
