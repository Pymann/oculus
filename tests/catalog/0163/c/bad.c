#include <setjmp.h>

static jmp_buf recover;

int guarded(void)
{
    return setjmp(recover);
}
