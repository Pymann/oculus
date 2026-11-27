#include <stdatomic.h>

void barrier(void)
{
    atomic_signal_fence(memory_order_seq_cst);
}
