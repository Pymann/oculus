#include <signal.h>

static volatile sig_atomic_t interrupted;

static void on_interrupt(int code)
{
    interrupted = 1;
    (void)signal(code, on_interrupt);
}

void install(void)
{
    (void)signal(SIGINT, on_interrupt);
}
