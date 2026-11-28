#include <stdio.h>
#include <string.h>

void read_line(char *buffer, int size, FILE *stream)
{
    if (fgets(buffer, size, stream) != NULL) {
        char *end = strchr(buffer, '\n');
        if (end != NULL) {
            *end = '\0';
        }
    }
}
