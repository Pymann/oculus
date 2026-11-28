#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>

int set_mode(const char *mode)
{
    static char entry[64];
    (void)snprintf(entry, sizeof entry, "MODE=%s", mode);
    return putenv(entry);
}

int set_level(const char *level)
{
    return setenv("LEVEL", level, 1);
}
