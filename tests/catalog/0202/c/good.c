#include <string.h>
#include <stdint.h>

struct point { int x; int y; };

int same_point(const struct point *left, const struct point *right)
{
    return (left->x == right->x) && (left->y == right->y);
}

int same_ids(const uint32_t left[4], const uint32_t right[4])
{
    return memcmp(left, right, 4U * sizeof left[0]) == 0;
}
