#include <threads.h>

static mtx_t lock;
static int value;

static void bump_locked(void)
{
    value++;
}

void bump_twice(void)
{
    (void)mtx_lock(&lock);
    bump_locked();
    bump_locked();
    (void)mtx_unlock(&lock);
}
