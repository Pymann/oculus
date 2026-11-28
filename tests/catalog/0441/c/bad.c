#include <string.h>

int same_value(const double *left, const double *right)
{
    return memcmp(left, right, sizeof *left) == 0;
}
