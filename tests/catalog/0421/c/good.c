#include <errno.h>
#include <limits.h>
#include <stdlib.h>

int port_of(const char *text, int *port)
{
    char *end = NULL;
    long value;
    errno = 0;
    value = strtol(text, &end, 10);
    if ((errno != 0) || (end == text) || (*end != '\0') || (value < 0) || (value > 65535)) {
        return 0;
    }
    *port = (int)value;
    return 1;
}
