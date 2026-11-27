#include <stdatomic.h>

static atomic_bool stop_requested;

void request_stop(void)
{
    atomic_store(&stop_requested, 1);
}
