#include <stdlib.h>

static int compare(const void *left, const void *right)
{
    return *(const int *)left - *(const int *)right;
}

void sort(int *items, size_t count)
{
    qsort(items, count, sizeof items[0], compare);
}
