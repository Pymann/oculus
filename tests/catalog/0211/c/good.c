#include <stdatomic.h>

atomic_int ready;

void publish(void)
{
    atomic_store(&ready, 1);
    atomic_store_explicit(&ready, 2, memory_order_seq_cst);
    ready += 1;
}
