#include <threads.h>
#include <time.h>

int try_for(const struct timespec *until)
{
    mtx_t lock;
    int result;
    (void)mtx_init(&lock, mtx_plain);
    result = mtx_timedlock(&lock, until);
    if (result == thrd_success) {
        (void)mtx_unlock(&lock);
    }
    mtx_destroy(&lock);
    return result;
}
