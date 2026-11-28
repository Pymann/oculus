#include <stdlib.h>

_Noreturn void fail(int code)
{
    if (code != 0) {
        exit(code);
    }
}
