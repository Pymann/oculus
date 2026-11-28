#include <stdio.h>

void save(FILE *stream)
{
    FILE copy = *stream;
    (void)copy;
}
