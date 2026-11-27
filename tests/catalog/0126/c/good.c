#include <stdio.h>
#include <string.h>

int save(FILE *file, int value, char *copy)
{
    int written = fprintf(file, "%d\n", value);
    (void)fflush(file);
    memcpy(copy, "x", 2U);   /* returns its argument: ignorable */
    return (fclose(file) == 0) && (written > 0);
}
