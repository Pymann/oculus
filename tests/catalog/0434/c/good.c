#include <stdio.h>

int first_of_each(FILE **streams)
{
    int item = fgetc(*streams++);
    FILE *stream = *streams;
    return item + getc(stream);
}
