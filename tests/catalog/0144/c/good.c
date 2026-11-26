#include <stdbool.h>

#define INLINE_HINT inline

static INLINE_HINT bool is_set(int value)
{
    return value != 0;
}
