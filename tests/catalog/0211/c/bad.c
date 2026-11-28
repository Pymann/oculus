#include <stdatomic.h>

atomic_int ready;

void publish(void)
{
    atomic_store_explicit(&ready, 1, memory_order_release);
}
