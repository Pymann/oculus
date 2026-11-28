#include <stdatomic.h>

static atomic_int total;

int start(void)
{
    atomic_int counter;
    atomic_int ready = 1;
    atomic_init(&counter, 0);
    return atomic_load(&counter) + atomic_load(&ready) + atomic_load(&total);
}
