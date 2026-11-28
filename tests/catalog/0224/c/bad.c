#include <threads.h>

static mtx_t lock;
static int value;

static void bump(void)
{
    (void)mtx_lock(&lock);
    value++;
    (void)mtx_unlock(&lock);
}

void bump_twice(void)
{
    (void)mtx_lock(&lock);
    bump();
    (void)mtx_unlock(&lock);
}
