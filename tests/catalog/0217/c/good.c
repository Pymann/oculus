#include <threads.h>

static int work(void *argument) { (void)argument; return 0; }

void run(int detach)
{
    thrd_t worker;
    if (thrd_create(&worker, work, NULL) == thrd_success) {
        if (detach != 0) {
            (void)thrd_detach(worker);
        } else {
            (void)thrd_join(worker, NULL);
        }
    }
}
