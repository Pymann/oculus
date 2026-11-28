#include <signal.h>

static volatile sig_atomic_t interrupted;

static void on_interrupt(int code)
{
    (void)code;
    interrupted = 1;
}

void install(void)
{
    (void)signal(SIGINT, on_interrupt);
}
