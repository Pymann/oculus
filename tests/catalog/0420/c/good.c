#include <stdio.h>
#include <stdlib.h>

static void on_exit_handler(void)
{
    (void)fflush(stdout);
}

int main(void)
{
    (void)atexit(on_exit_handler);
    return 0;
}
