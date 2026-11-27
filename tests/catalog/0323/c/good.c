#include <stdio.h>

void label(char *target, size_t size, const char *name)
{
    (void)snprintf(target, size, "%s", name);
}
