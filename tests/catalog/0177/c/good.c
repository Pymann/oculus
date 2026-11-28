#include <stdio.h>

void finish(FILE *stream, const char *next)
{
    (void)fflush(stream);
    (void)fclose(stream);
    stream = fopen(next, "w");
    if (stream != NULL) {
        (void)fclose(stream);
    }
}
