#include <stdio.h>

void read_line(char *buffer, int size, FILE *stream)
{
    if (fgets(buffer, size, stream) == NULL) {
        buffer[0] = '\0';
    }
    (void)puts(buffer);
}
