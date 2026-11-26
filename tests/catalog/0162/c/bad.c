#include <stdlib.h>

int *make_buffer(size_t count)
{
    return malloc(count * sizeof(int));
}
