#include <stdio.h>

void mark(const char *path)
{
    FILE *stream = fopen(path, "r+");
    if (stream != NULL) {
        (void)fputs("seen\n", stream);
        (void)fclose(stream);
    }
}
