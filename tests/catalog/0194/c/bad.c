#include <stdlib.h>

void fail(const char *reason)
{
    (void)reason;
    abort();
}
