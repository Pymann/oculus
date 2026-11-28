#include <threads.h>

static int work(void *argument) { (void)argument; return 0; }

void run(void)
{
    thrd_t worker;
    if (thrd_create(&worker, work, NULL) == thrd_success) {
        (void)thrd_detach(worker);
        (void)thrd_join(worker, NULL);
    }
}
