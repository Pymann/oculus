#include <stdio.h>

void save(FILE *file, int value)
{
    fprintf(file, "%d\n", value);
    fclose(file);
}
