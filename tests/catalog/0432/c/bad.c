#include <stdio.h>
#include <string.h>

void read_line(char *buffer, int size, FILE *stream)
{
    if (fgets(buffer, size, stream) != NULL) {
        buffer[strlen(buffer) - 1U] = '\0';
    }
}
