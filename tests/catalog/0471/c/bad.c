#include <signal.h>

static volatile sig_atomic_t failed;

static void on_fpe(int code)
{
    (void)code;
    failed = 1;
}

void install(void)
{
    (void)signal(SIGFPE, on_fpe);
}
