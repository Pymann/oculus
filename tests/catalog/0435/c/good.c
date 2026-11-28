#include <stdio.h>

void reread(FILE *stream)
{
    fpos_t position;
    if (fgetpos(stream, &position) == 0) {
        (void)getc(stream);
        (void)fsetpos(stream, &position);
    }
}
