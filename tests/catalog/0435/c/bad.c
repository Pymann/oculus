#include <stdio.h>
#include <string.h>

void rewind_to_start(FILE *stream)
{
    fpos_t position;
    (void)memset(&position, 0, sizeof position);
    (void)fsetpos(stream, &position);
}
