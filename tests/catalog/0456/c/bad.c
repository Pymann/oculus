#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <unistd.h>

void show_link(const char *path)
{
    char target[256];
    if (readlink(path, target, sizeof target) != -1) {
        (void)puts(target);
    }
}
