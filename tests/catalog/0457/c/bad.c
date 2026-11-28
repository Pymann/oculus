#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>

int set_mode(const char *mode)
{
    char entry[64];
    (void)snprintf(entry, sizeof entry, "MODE=%s", mode);
    return putenv(entry);
}
