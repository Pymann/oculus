#include <stdio.h>

void save(FILE *stream)
{
    if (stream != NULL) {
        (void)fflush(stream);
    }
}
