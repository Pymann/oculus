#include <stdatomic.h>
#include <stdbool.h>

atomic_bool flag;
atomic_int count;

void toggle(void)
{
    bool expected = atomic_load(&flag);
    while (!atomic_compare_exchange_weak(&flag, &expected, !expected)) {
    }
    count += 1;
    (void)atomic_fetch_add(&count, 1);
}
