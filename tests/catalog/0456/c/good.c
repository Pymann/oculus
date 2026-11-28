#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <unistd.h>

void show_link(const char *path)
{
    char target[256];
    ssize_t length = readlink(path, target, sizeof target - 1U);
    if (length != -1) {
        target[length] = '\0';
        (void)puts(target);
    }
}
