#include <stddef.h>

#define POOL_SIZE 64U

static int pool[POOL_SIZE];
static size_t pool_used;

int *make_buffer(size_t count)
{
    int *result = NULL;
    if (count <= (POOL_SIZE - pool_used)) {
        result = &pool[pool_used];
        pool_used += count;
    }
    return result;
}
