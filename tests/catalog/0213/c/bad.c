#include <stdio.h>

int count_lines(FILE *stream)
{
    int lines = 0;
    char item;
    while ((item = (char)getc(stream)), item != EOF) {
        if (item == '\n') {
            lines++;
        }
    }
    return lines;
}
