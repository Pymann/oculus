#include <threads.h>

static mtx_t lock;
static cnd_t changed;
static int ready;

void wait_ready(void)
{
    (void)mtx_lock(&lock);
    if (ready == 0) {
        (void)cnd_wait(&changed, &lock);
    }
    (void)mtx_unlock(&lock);
}
