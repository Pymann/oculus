#include <signal.h>

static void on_interrupt(int code)
{
    (void)code;
}

void install(void)
{
    (void)signal(SIGINT, on_interrupt);
}
