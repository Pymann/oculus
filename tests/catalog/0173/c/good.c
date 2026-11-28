#include <stdlib.h>

void drop(void)
{
    int *items = calloc(4U, sizeof *items);
    free(items);
    free(NULL);
}
