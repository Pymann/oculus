#include <threads.h>

void finish(mtx_t *lock)
{
    (void)mtx_lock(lock);
    mtx_destroy(lock);
}
