#include <threads.h>

static mtx_t lock;
static int value;

int take(void)
{
    (void)mtx_lock(&lock);
    if (value == 0) {
        return -1;
    }
    value--;
    (void)mtx_unlock(&lock);
    return 0;
}
