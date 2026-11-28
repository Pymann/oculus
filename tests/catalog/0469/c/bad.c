#include <signal.h>

static int interrupt_count;

static void on_interrupt(int code)
{
    (void)code;
    interrupt_count++;
}

void install(void)
{
    (void)signal(SIGINT, on_interrupt);
}
