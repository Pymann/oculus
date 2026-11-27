#include <errno.h>
#include <stdlib.h>

long parse(const char *text)
{
    int saved = errno;
    long value;
    errno = 0;
    value = strtol(text, NULL, 10);
    if (errno == 0) {
        errno = saved;   /* restore the caller's value */
    }
    return value;
}
