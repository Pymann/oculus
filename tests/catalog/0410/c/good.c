#include <signal.h>

static volatile sig_atomic_t stopped;

static void on_stop(int number) { (void)number; stopped = 1; }

void run(void)
{
    (void)signal(SIGINT, on_stop);
}
