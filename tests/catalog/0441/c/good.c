#include <string.h>

void serialise(unsigned char out[sizeof(double)], const double *value)
{
    (void)memcpy(out, value, sizeof *value);
}
