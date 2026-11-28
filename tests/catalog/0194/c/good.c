#include <stdlib.h>

_Noreturn void fail(const char *reason)
{
    (void)reason;
    abort();
}

void maybe_fail(int code)
{
    if (code != 0) {
        abort();
    }
}
