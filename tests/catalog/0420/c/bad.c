#include <stdlib.h>

static void on_exit_handler(void)
{
    exit(1);
}

int main(void)
{
    (void)atexit(on_exit_handler);
    return 0;
}
