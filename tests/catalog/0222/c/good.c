#include <threads.h>

static mtx_t lock;
static int value;

int take(void)
{
    int result = -1;
    if (mtx_trylock(&lock) != thrd_success) {
        return -1;
    }
    if (value != 0) {
        value--;
        result = 0;
    }
    (void)mtx_unlock(&lock);
    return result;
}
