#include <stdio.h>

FILE *open_config(void)
{
    return fopen("/dev/tty", "r");
}
