#include <signal.h>
#include <stdlib.h>

static void on_fpe(int code)
{
    (void)code;
    _Exit(EXIT_FAILURE);
}

void install(void)
{
    (void)signal(SIGFPE, on_fpe);
}
