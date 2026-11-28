#include <stdio.h>

void read_line(char *buffer, int size, FILE *stream)
{
    if (fgets(buffer, size, stream) == NULL) {
        (void)puts(buffer);
    }
}
