#include <signal.h>
#include <stdio.h>

static void on_interrupt(int code)
{
    printf("signal %d\n", code);
}

void install(void)
{
    (void)signal(SIGINT, on_interrupt);
}
