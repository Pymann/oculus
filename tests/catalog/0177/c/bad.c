#include <stdio.h>

void finish(FILE *stream)
{
    (void)fclose(stream);
    (void)fflush(stream);
}
