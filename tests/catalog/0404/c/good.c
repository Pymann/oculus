#include <threads.h>

void finish(mtx_t *lock)
{
    (void)mtx_lock(lock);
    (void)mtx_unlock(lock);
    mtx_destroy(lock);
}
