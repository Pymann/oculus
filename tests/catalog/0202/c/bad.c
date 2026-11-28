#include <string.h>

struct point { int x; int y; };

int same_point(const struct point *left, const struct point *right)
{
    return memcmp(left, right, sizeof *left) == 0;
}
