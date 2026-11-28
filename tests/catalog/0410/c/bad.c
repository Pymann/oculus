#include <signal.h>
#include <threads.h>

static void on_stop(int number) { (void)number; }
static int work(void *argument) { (void)argument; return 0; }

void run(void)
{
    thrd_t worker;
    (void)signal(SIGINT, on_stop);
    if (thrd_create(&worker, work, NULL) == thrd_success) {
        (void)thrd_join(worker, NULL);
    }
}
