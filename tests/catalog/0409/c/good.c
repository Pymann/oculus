#include <stdatomic.h>
#include <threads.h>

static mtx_t lock;
static cnd_t changed;
static int ready;
static atomic_int counter;

void wait_ready(void)
{
    (void)mtx_lock(&lock);
    while (ready == 0) {
        (void)cnd_wait(&changed, &lock);
    }
    (void)mtx_unlock(&lock);
}

void increment(void)
{
    int expected = atomic_load(&counter);
    while (!atomic_compare_exchange_weak(&counter, &expected, expected + 1)) {
    }
}

int claim(void)
{
    int expected = 0;
    return atomic_compare_exchange_strong(&counter, &expected, 1);
}
