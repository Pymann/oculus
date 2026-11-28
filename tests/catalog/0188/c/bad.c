#include <stdatomic.h>

int start(void)
{
    atomic_int counter;
    return atomic_load(&counter);
}
