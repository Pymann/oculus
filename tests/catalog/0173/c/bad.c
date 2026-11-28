#include <stdlib.h>

void drop(void)
{
    int items[4] = { 0 };
    free(items);
}
