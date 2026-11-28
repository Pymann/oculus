#include <stdio.h>

int count_lines(FILE *stream)
{
    int lines = 0;
    int item = getc(stream);
    while (item != EOF) {
        if (item == '\n') {
            lines++;
        }
        item = getc(stream);
    }
    return lines;
}
